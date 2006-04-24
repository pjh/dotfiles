
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

struct dbfs_dirscan_info {
	const char		*name;
	size_t			namelen;
	void			*start_ent;
	void			*end_ent;
};

static DB_ENV *db_env;
static DB *db_meta;

void dbfs_init(void *userdata)
{
	const char *db_home, *db_password;
	int rc;
	unsigned int flags = 0;

	/*
	 * open DB environment
	 */

	db_home = getenv("DB_HOME");
	if (!db_home) {
		fprintf(stderr, "DB_HOME not set\n");
		exit(1);
	}

	/* this isn't a very secure way to handle passwords */
	db_password = getenv("DB_PASSWORD");

	rc = db_env_create(&db_env, 0);
	if (rc) {
		fprintf(stderr, "db_env_create failed: %d\n", rc);
		exit(1);
	}

	/* stderr is wrong; should use syslog instead */
	db_env->set_errfile(db_env, stderr);
	db_env->set_errpfx(db_env, "dbfs");

	if (db_password) {
		flags |= DB_ENCRYPT;
		rc = db_env->set_encrypt(db_env, db_password, DB_ENCRYPT_AES);
		if (rc) {
			db_env->err(db_env, rc, "db_env->set_encrypt");
			goto err_out;
		}

		/* this isn't a very good way to shroud the password */
		if (putenv("DB_PASSWORD=X"))
			perror("putenv (SECURITY WARNING)");
	}

	/* init DB transactional environment, stored in directory db_home */
	rc = db_env->open(db_env, db_home,
			  DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL |
			  DB_INIT_TXN | DB_RECOVER | DB_CREATE | flags, 0666);
	if (rc) {
		db_env->err(db_env, rc, "db_env->open");
		goto err_out;
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
			   DB_HASH, DB_AUTO_COMMIT | DB_CREATE | flags, 0666);
	if (rc) {
		db_meta->err(db_meta, rc, "db_meta->open");
		goto err_out_meta;
	}

	/* our data items are small, so use the smallest possible page
	 * size.  This is a guess, and should be verified by looking at
	 * overflow pages and other DB statistics.
	 */
	rc = db_meta->set_pagesize(db_meta, 512);
	if (rc) {
		db_meta->err(db_meta, rc, "db_meta->set_pagesize");
		goto err_out_meta;
	}

	/* fix everything as little endian */
	rc = db_meta->set_lorder(db_meta, 1234);
	if (rc) {
		db_meta->err(db_meta, rc, "db_meta->set_lorder");
		goto err_out_meta;
	}

	return;

err_out_meta:
	db_meta->close(db_meta, 0);
err_out:
	db_env->close(db_env, 0);
	exit(1);
}

void dbfs_exit(void *userdata)
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

	/* delete key 'key_str' from metadata database */
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

static int dbfs_mode_type(guint32 mode, enum dbfs_inode_type *itype)
{
	if (S_ISDIR(mode))
		*itype = IT_DIR;
	else if (S_ISCHR(mode) || S_ISBLK(mode))
		*itype = IT_DEV;
	else if (S_ISFIFO(mode))
		*itype = IT_FIFO;
	else if (S_ISLNK(mode))
		*itype = IT_SYMLINK;
	else if (S_ISSOCK(mode))
		*itype = IT_SOCKET;
	else if (S_ISREG(mode))
		*itype = IT_REG;
	else
		return -EINVAL;
	
	return 0;
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

	/* calculate size of struct dbfs_extent area */
	ex_sz = val.size - sizeof(struct dbfs_raw_inode);

	/* initialize runtime information about the inode */
	ino = g_new(struct dbfs_inode, 1);
	ino->n_extents = ex_sz / sizeof(struct dbfs_extent);
	ino->raw_ino_size = val.size;
	ino->raw_inode = val.data;

	/* deduce inode type */
	mode = GUINT32_FROM_LE(ino->raw_inode->mode);
	rc = dbfs_mode_type(mode, &ino->type);
	g_assert(rc == 0);

	*ino_out = ino;

	return 0;
}

int dbfs_symlink_read(guint64 ino, DBT *val)
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

int dbfs_dir_read(guint64 ino, DBT *val)
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

static int dbfs_dir_write(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return db_meta->put(db_meta, NULL, &key, val, 0) ? -EIO : 0;
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

		/* send dirent to callback function */
		rc = func(de, userdata);
		if (rc)
			break;

		/* align things so that compiler structures
		 * do not wind up misaligned.
		 */
		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	return rc;
}

static int dbfs_dir_scan1(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	if (!di->start_ent) {
		if ((de->namelen == di->namelen) &&
		    (!memcmp(de->name, di->name, di->namelen)))
			di->start_ent = de;
	}
	else if (!di->end_ent) {
		di->end_ent = de;
		return 1;
	}

	return 0;
}

int dbfs_dir_lookup(guint64 parent, const char *name, guint64 *ino)
{
	struct dbfs_dirscan_info di;
	struct dbfs_dirent *de;
	DBT val;
	int rc;

	*ino = 0;

	/* read directory from database */
	rc = dbfs_dir_read(parent, &val);
	if (rc)
		return rc;

	memset(&di, 0, sizeof(di));
	di.name = name;
	di.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_scan1, &di);
	if (!rc || !di.start_ent) {
		rc = -ENOENT;
		goto out;
	}
	if (rc != 1)
		goto out;

	/* if match found, return inode number */
	de = di.start_ent;
	*ino = de->ino;

out:
	free(val.data);
	return rc;
}

static int dbfs_dirent_del(guint64 parent, const char *name)
{
	struct dbfs_dirscan_info ui;
	DBT dir_val;
	int rc, del_len, tail_len;

	rc = dbfs_dir_read(parent, &dir_val);
	if (rc)
		return rc;

	memset(&ui, 0, sizeof(ui));
	ui.name = name;
	ui.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(dir_val.data, dbfs_dir_scan1, &ui);
	if (rc != 1) {
		free(dir_val.data);
		return -ENOENT;
	}

	del_len = ui.end_ent - ui.start_ent;
	tail_len = (dir_val.data + dir_val.size) - ui.end_ent;

	memmove(ui.start_ent, ui.end_ent, tail_len);
	dir_val.size -= del_len;

	rc = dbfs_dir_write(parent, &dir_val);

	free(dir_val.data);

	return rc;
}

int dbfs_unlink(guint64 parent, const char *name, unsigned long flags)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc, is_dir;
	guint32 nlink;

	rc = dbfs_dir_lookup(parent, name, &ino_n);
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

int dbfs_mknod(guint64 parent, const char *name, guint32 mode, guint64 rdev,
	       struct dbfs_inode **ino)
{
	/* FIXME */
	*ino = NULL;
	return -EIO;
}

