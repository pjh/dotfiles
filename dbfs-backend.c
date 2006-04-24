
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

static DB_ENV *db_env;
static DB *db_meta;

int init_db(void)
{
	const char *db_home;
	int rc;

	/*
	 * open DB environment
	 */

	db_home = getenv("DB_HOME");
	if (!db_home) {
		fprintf(stderr, "DB_HOME not set\n");
		return 1;
	}

	rc = db_env_create(&db_env, 0);
	if (rc) {
		fprintf(stderr, "db_env_create failed: %d\n", rc);
		return 1;
	}

	db_env->set_errfile(db_env, stderr);
	db_env->set_errpfx(db_env, "dbfs");

	rc = db_env->open(db_env, db_home,
			  DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL |
			  DB_INIT_TXN | DB_RECOVER | DB_CREATE, 0666);
	if (rc) {
		db_env->err(db_env, rc, "db_env->open");
		return 1;
	}

	/*
	 * Open metadata database
	 */

	rc = db_create(&db_meta, db_env, 0);
	if (rc) {
		db_env->err(db_env, rc, "db_create");
		goto err_out;
	}

	rc = db_meta->open(db_meta, NULL, "metadata", NULL,
			   DB_HASH, DB_AUTO_COMMIT | DB_CREATE, 0666);
	if (rc) {
		db_meta->err(db_meta, rc, "db_meta->open");
		goto err_out_meta;
	}

	return 0;

err_out_meta:
	db_meta->close(db_meta, 0);
err_out:
	db_env->close(db_env, 0);
	return 1;
}

void exit_db(void)
{
	db_meta->close(db_meta, 0);
	db_env->close(db_env, 0);

	db_env = NULL;
	db_meta = NULL;
}

void dbfs_inode_free(struct dbfs_inode *ino)
{
	free(ino->raw_inode);
	g_free(ino);
}

static int dbmeta_del(const char *key_str)
{
	DBT key;
	int rc;

	key.data = (void *) key_str;
	key.size = strlen(key_str);

	rc = db_meta->del(db_meta, NULL, &key, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return -EIO;
	return 0;
}

static int dbfs_inode_del(struct dbfs_inode *ino)
{
	guint64 ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	char key[32];
	int rc, rrc;

	sprintf(key, "/inode/%Lu", (unsigned long long) ino_n);

	rrc = dbmeta_del(key);

	switch (ino->type) {
	case IT_REG:
		/* FIXME */
		break;

	case IT_DIR:
		sprintf(key, "/dir/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(key);
		if (rc && !rrc)
			rrc = rc;
		break;

	case IT_SYMLINK:
		sprintf(key, "/symlink/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(key);
		if (rc && !rrc)
			rrc = rc;
		break;

	case IT_DEV:
	case IT_FIFO:
	case IT_SOCKET:
		/* nothing additional to delete */
		break;
	}

	return rrc;
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

	raw_ino->version = GUINT64_TO_LE(
		GUINT64_FROM_LE(raw_ino->version) + 1);

	return db_meta->get(db_meta, NULL, &key, &val, 0) ? -EIO : 0;
}

int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_inode *ino;
	size_t ex_sz;
	guint32 mode;

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

	mode = GUINT32_FROM_LE(ino->raw_inode->mode);
	if (S_ISDIR(mode))
		ino->type = IT_DIR;
	else if (S_ISCHR(mode) || S_ISBLK(mode))
		ino->type = IT_DEV;
	else if (S_ISFIFO(mode))
		ino->type = IT_FIFO;
	else if (S_ISLNK(mode))
		ino->type = IT_SYMLINK;
	else if (S_ISSOCK(mode))
		ino->type = IT_SOCKET;
	else {
		g_assert(S_ISREG(mode));
		ino->type = IT_REG;
	}

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

	if (ino_n == DBFS_ROOT_INO) {
		rc = -EINVAL;
		goto out;
	}

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
	if (is_dir && (nlink <= 2))
		nlink = 0;
	else
		nlink--;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	if (!nlink)
		rc = dbfs_inode_del(ino);
	else
		rc = dbfs_inode_write(ino);

out_ino:
	dbfs_inode_free(ino);
out:
	return rc;
}

