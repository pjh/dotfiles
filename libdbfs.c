
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs *gfs;

int dbfs_open(struct dbfs *fs, unsigned int flags, const char *errpfx)
{
	const char *db_home, *db_password;
	int rc;

	/*
	 * open DB environment
	 */

	db_home = fs->home;
	if (!db_home) {
		fprintf(stderr, "DB_HOME not set\n");
		return -EINVAL;
	}

	/* this isn't a very secure way to handle passwords */
	db_password = fs->passwd;

	rc = db_env_create(&fs->env, 0);
	if (rc) {
		fprintf(stderr, "fs->env_create failed: %d\n", rc);
		return rc;
	}

	/* stderr is wrong; should use syslog instead */
	fs->env->set_errfile(fs->env, stderr);
	fs->env->set_errpfx(fs->env, errpfx);

	if (db_password) {
		flags |= DB_ENCRYPT;
		rc = fs->env->set_encrypt(fs->env, db_password, DB_ENCRYPT_AES);
		if (rc) {
			fs->env->err(fs->env, rc, "fs->env->set_encrypt");
			goto err_out;
		}

		memset(fs->passwd, 0, strlen(fs->passwd));
		free(fs->passwd);
		fs->passwd = NULL;
	}

	/* init DB transactional environment, stored in directory db_home */
	rc = fs->env->open(fs->env, db_home,
			  DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL |
			  DB_INIT_TXN | DB_RECOVER | flags, 0666);
	if (rc) {
		fs->env->err(fs->env, rc, "fs->env->open");
		goto err_out;
	}

	/*
	 * Open metadata database
	 */

	rc = db_create(&fs->meta, fs->env, 0);
	if (rc) {
		fs->env->err(fs->env, rc, "db_create");
		goto err_out;
	}

	rc = fs->meta->open(fs->meta, NULL, "metadata", NULL,
			   DB_HASH, DB_AUTO_COMMIT | flags, 0666);
	if (rc) {
		fs->meta->err(fs->meta, rc, "fs->meta->open");
		goto err_out_meta;
	}

	/* our data items are small, so use the smallest possible page
	 * size.  This is a guess, and should be verified by looking at
	 * overflow pages and other DB statistics.
	 */
	rc = fs->meta->set_pagesize(fs->meta, 512);
	if (rc) {
		fs->meta->err(fs->meta, rc, "fs->meta->set_pagesize");
		goto err_out_meta;
	}

	/* fix everything as little endian */
	rc = fs->meta->set_lorder(fs->meta, 1234);
	if (rc) {
		fs->meta->err(fs->meta, rc, "fs->meta->set_lorder");
		goto err_out_meta;
	}

	gfs = fs;
	return 0;

err_out_meta:
	fs->meta->close(fs->meta, 0);
err_out:
	fs->env->close(fs->env, 0);
	return rc;
}

void dbfs_close(struct dbfs *fs)
{
	fs->meta->close(fs->meta, 0);
	fs->env->close(fs->env, 0);

	fs->env = NULL;
	fs->meta = NULL;
}

struct dbfs *dbfs_new(void)
{
	struct dbfs *fs;
	char *passwd;

	fs = g_new0(struct dbfs, 1);
	if (!fs)
		return NULL;

	fs->next_inode = 2ULL;

	fs->home = getenv("DB_HOME");
	if (!fs->home)
		goto err_out;

	passwd = getenv("DB_PASSWORD");
	if (passwd) {
		fs->passwd = strdup(passwd);

		/* FIXME: this isn't a very good way to shroud the password */
		if (putenv("DB_PASSWORD=X"))
			perror("putenv DB_PASSWORD (SECURITY WARNING)");
	}

	return fs;

err_out:
	g_free(fs);
	return NULL;
}

void dbfs_free(struct dbfs *fs)
{
	g_free(fs);
}

void dbfs_inode_free(struct dbfs_inode *ino)
{
	if (!ino)		/* permit dbfs_inode_free(NULL) */
		return;

	free(ino->raw_inode);
	g_free(ino);
}

int dbfs_inode_write(struct dbfs_inode *ino)
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

	return gfs->meta->put(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;
}

int dbfs_dir_new(guint64 parent, guint64 ino_n, const struct dbfs_inode *ino)
{
	void *mem, *p, *q;
	struct dbfs_dirent *de;
	size_t namelen;
	DBT val;
	int rc;

	p = mem = malloc(128);
	memset(mem, 0, 128);

	/*
	 * add entry for "."
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);
	de->namelen = GUINT16_TO_LE(1);
	de->ino = GUINT64_TO_LE(ino_n);

	q = p + sizeof(struct dbfs_dirent);
	memcpy(q, ".", 1);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += dbfs_dirent_next(namelen);

	/*
	 * add entry for ".."
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);
	de->namelen = GUINT16_TO_LE(2);
	de->ino = GUINT64_TO_LE(parent);

	q = p + sizeof(struct dbfs_dirent);
	memcpy(q, "..", 2);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += dbfs_dirent_next(namelen);

	/*
	 * add terminating entry
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += dbfs_dirent_next(namelen);

	/*
	 * store dir in database
	 */
	memset(&val, 0, sizeof(val));
	val.data = mem;
	val.size = p - mem;

	rc = dbfs_dir_write(ino_n, &val);

	free(mem);

	return rc;
}

int dbfs_dir_write(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return gfs->meta->put(gfs->meta, NULL, &key, val, 0) ? -EIO : 0;
}

