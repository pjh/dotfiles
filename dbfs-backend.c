
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

int dbfs_read_inode(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_raw_inode *raw_ino;
	struct dbfs_inode *ino;
	size_t ex_sz, i;

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

	raw_ino = val.data;
	raw_ino->ino		= GUINT64_FROM_LE(raw_ino->ino);
	raw_ino->generation	= GUINT64_FROM_LE(raw_ino->generation);
	raw_ino->mode		= GUINT32_FROM_LE(raw_ino->mode);
	raw_ino->nlink		= GUINT32_FROM_LE(raw_ino->nlink);
	raw_ino->uid		= GUINT32_FROM_LE(raw_ino->uid);
	raw_ino->gid		= GUINT32_FROM_LE(raw_ino->gid);
	raw_ino->rdev		= GUINT64_FROM_LE(raw_ino->rdev);
	raw_ino->size		= GUINT64_FROM_LE(raw_ino->size);
	raw_ino->ctime		= GUINT64_FROM_LE(raw_ino->ctime);
	raw_ino->atime		= GUINT64_FROM_LE(raw_ino->atime);
	raw_ino->mtime		= GUINT64_FROM_LE(raw_ino->mtime);

	ex_sz = val.size - sizeof(struct dbfs_raw_inode);
	i = sizeof(struct dbfs_inode) - sizeof(struct dbfs_raw_inode);

	ino = g_malloc(i + ex_sz + sizeof(struct dbfs_raw_inode));
	memcpy(&ino->raw_inode, raw_ino, val.size);
	ino->n_extents = ex_sz / sizeof(struct dbfs_extent);

	*ino_out = ino;

	free(val.data);

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
	return rc;
}

static int dbfs_write_dir(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return db_meta->put(db_meta, NULL, &key, val, 0);
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
	return rc;
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

int dbfs_unlink(guint64 parent, const char *name)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc;

	rc = dbfs_lookup(parent, name, &ino_n);
	if (rc)
		goto err_out;

	rc = dbfs_read_inode(ino_n, &ino);
	if (rc)
		goto err_out;

	rc = dbfs_dirent_del(parent, name);

	/* FIXME stopped working here...

	 * decrement n_links
	 * if n_links==0, delete inode
	 */

err_out:
	return rc;
}

