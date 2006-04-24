
#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs_lookup_info {
	const char	*name;
	size_t		namelen;
	guint64		*ino;
};

struct dbfs_unlink_info {
	const char		*name;
	size_t			namelen;
	void			*start_ent;
	void			*end_ent;
};

static DB_ENV *env;
static DB *db_data;
static DB *db_meta;

void dbfs_dummy1(void)
{
	(void) env;
	(void) db_data;
}

void dbfs_inode_free(struct dbfs_inode *ino)
{
	free(ino->raw_inode);
	g_free(ino);
}

static int dbfs_inode_del(guint64 ino_n)
{
	/* FIXME */
	return -EIO;
}

static int dbfs_inode_write(struct dbfs_inode *ino)
{
	struct dbfs_raw_inode *raw_ino = ino->raw_inode;
	guint64 ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	DBT key, val;
	char key_str[32];

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	key.data = key_str;
	key.size = strlen(key_str);

	val.data = raw_ino;
	val.size = ino->raw_ino_size;

	return db_meta->get(db_meta, NULL, &key, &val, 0) ? -EIO : 0;
}

int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_inode *ino;
	size_t ex_sz;

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	key.data = key_str;
	key.size = strlen(key_str);

	val.flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, &val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return rc;

	ex_sz = val.size - sizeof(struct dbfs_raw_inode);

	ino = g_new(struct dbfs_inode, 1);
	ino->n_extents = ex_sz / sizeof(struct dbfs_extent);
	ino->raw_ino_size = val.size;
	ino->raw_inode = val.data;

	*ino_out = ino;

	return 0;
}

int dbfs_read_dir(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];
	int rc;

	memset(&key, 0, sizeof(key));
	memset(val, 0, sizeof(*val));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	val->flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOTDIR;
	return rc ? -EIO : 0;
}

static int dbfs_write_dir(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return db_meta->put(db_meta, NULL, &key, val, 0) ? -EIO : 0;
}

int dbfs_read_link(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];
	int rc;

	memset(&key, 0, sizeof(key));
	memset(val, 0, sizeof(*val));

	sprintf(key_str, "/symlink/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	val->flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata)
{
	struct dbfs_dirent *de;
	void *p;
	int rc = 0;

	p = dir;
	while (1) {
		de = p;
		de->magic	= GUINT32_FROM_LE(de->magic);
		de->namelen	= GUINT16_FROM_LE(de->namelen);
		de->ino		= GUINT64_FROM_LE(de->ino);

		g_assert (de->magic == DBFS_DE_MAGIC);
		if (!de->namelen)
			break;

		rc = func(de, userdata);
		if (rc)
			break;

		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	return rc;
}

static int dbfs_dir_cmp(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_lookup_info *li = userdata;

	if ((li->namelen == de->namelen) &&
	    (!memcmp(li->name, de->name, li->namelen))) {
	    	*li->ino = de->ino;
		return 1;
	}

	return 0;
}

int dbfs_lookup(guint64 parent, const char *name, guint64 *ino)
{
	struct dbfs_lookup_info li;
	size_t namelen = strlen(name);
	DBT val;
	int rc;

	*ino = 0;

	rc = dbfs_read_dir(parent, &val);
	if (rc)
		return rc;

	li.name = name;
	li.namelen = namelen;
	li.ino = ino;
	rc = dbfs_dir_foreach(val.data, dbfs_dir_cmp, &li);
	if (rc)
		rc = 0;
	else
		rc = -ENOENT;

	free(val.data);
	return rc;
}

static int dbfs_dir_scan1(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_unlink_info *ui = userdata;

	if (!ui->start_ent) {
		if ((de->namelen == ui->namelen) &&
		    (!memcmp(de->name, ui->name, ui->namelen)))
			ui->start_ent = de;
	}
	else if (!ui->end_ent) {
		ui->end_ent = de;
		return 1;
	}

	return 0;
}

static int dbfs_dirent_del(guint64 parent, const char *name)
{
	struct dbfs_unlink_info ui;
	DBT dir_val;
	int rc, del_len, tail_len;

	rc = dbfs_read_dir(parent, &dir_val);
	if (rc)
		return rc;

	memset(&ui, 0, sizeof(ui));
	ui.name = name;
	ui.namelen = strlen(name);

	rc = dbfs_dir_foreach(dir_val.data, dbfs_dir_scan1, &ui);
	if (rc != 1) {
		free(dir_val.data);
		return -ENOENT;
	}

	del_len = ui.end_ent - ui.start_ent;
	tail_len = (dir_val.data + dir_val.size) - ui.end_ent;

	memmove(ui.start_ent, ui.end_ent, tail_len);
	dir_val.size -= del_len;

	rc = dbfs_write_dir(parent, &dir_val);

	free(dir_val.data);

	return rc;
}

int dbfs_unlink(guint64 parent, const char *name, unsigned long flags)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc, is_dir;
	guint32 nlink;

	rc = dbfs_lookup(parent, name, &ino_n);
	if (rc)
		goto out;

	rc = dbfs_inode_read(ino_n, &ino);
	if (rc)
		goto out;

	is_dir = S_ISDIR(GUINT32_FROM_LE(ino->raw_inode->mode));
	if (is_dir && (!(flags & DBFS_UNLINK_DIR))) {
		rc = -EISDIR;
		goto out_ino;
	}

	rc = dbfs_dirent_del(parent, name);
	if (rc)
		goto out_ino;

	nlink = GUINT32_FROM_LE(ino->raw_inode->nlink);
	nlink--;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	if ((is_dir && (nlink < 2)) ||
	    (!is_dir && (nlink < 1)))
		rc = dbfs_inode_del(ino_n);
	else
		rc = dbfs_inode_write(ino);

out_ino:
	dbfs_inode_free(ino);
out:
	return rc;
}

