
/*
 * Maintained by Jeff Garzik <jgarzik@pobox.com>
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs *gfs;

static void dbfs_db_syslog(const DB_ENV *dbenv, const char *errpfx,
			const char *msg)
{
	syslog(LOG_WARNING, "%s: %s", errpfx, msg);
}

static int open_db(DB_ENV *env, DB **db_out, const char *name,
		   unsigned int page_size, unsigned int flags)
{
	int rc;
	DB *db;

	rc = db_create(db_out, env, 0);
	if (rc) {
		env->err(env, rc, "db_create");
		return -EIO;
	}

	db = *db_out;

	rc = db->set_pagesize(db, page_size);
	if (rc) {
		db->err(db, rc, "db->set_pagesize");
		return -EIO;
	}

	/* fix everything as little endian */
	rc = db->set_lorder(db, 1234);
	if (rc) {
		db->err(db, rc, "db->set_lorder");
		return -EIO;
	}

	rc = db->open(db, NULL, name, NULL, DB_HASH,
		      DB_AUTO_COMMIT | flags, 0666);
	if (rc) {
		db->err(db, rc, "db->open");
		return -EIO;
	}

	return 0;
}

int dbfs_open(struct dbfs *fs, unsigned int env_flags, unsigned int flags,
	      const char *errpfx, gboolean syslog)
{
	const char *db_home, *db_password;
	int rc;

	/*
	 * open DB environment
	 */

	db_home = fs->home;
	g_assert(db_home != NULL);

	/* this isn't a very secure way to handle passwords */
	db_password = fs->passwd;

	rc = db_env_create(&fs->env, 0);
	if (rc) {
		fprintf(stderr, "fs->env_create failed: %d\n", rc);
		return rc;
	}

	if (syslog)
		fs->env->set_errcall(fs->env, dbfs_db_syslog);
	else
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
			  DB_INIT_TXN | env_flags, 0666);
	if (rc) {
		fs->env->err(fs->env, rc, "fs->env->open");
		goto err_out;
	}

	/*
	 * Open metadata database
	 */

	rc = open_db(fs->env, &fs->meta, "metadata", DBFS_PGSZ_METADATA, flags);
	if (rc)
		goto err_out;

	rc = open_db(fs->env, &fs->hashref, "hash", DBFS_PGSZ_METADATA, flags);
	if (rc)
		goto err_out;

	rc = open_db(fs->env, &fs->data, "data", DBFS_PGSZ_DATA, flags);
	if (rc)
		goto err_out_meta;

	return 0;

err_out_meta:
	fs->meta->close(fs->meta, 0);
err_out:
	fs->env->close(fs->env, 0);
	return rc;
}

void dbfs_close(struct dbfs *fs)
{
	fs->data->close(fs->data, 0);
	fs->hashref->close(fs->hashref, 0);
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
	if (!fs->home) {
		fprintf(stderr, "DB_HOME not set, aborting\n");
		goto err_out;
	}

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

int dbfs_inode_write(DB_TXN *txn, struct dbfs_inode *ino)
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

	return gfs->meta->put(gfs->meta, txn, &key, &val, 0) ? -EIO : 0;
}

int dbfs_dir_new(DB_TXN *txn, guint64 parent, guint64 ino_n,
		 const struct dbfs_inode *ino)
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

	rc = dbfs_dir_write(txn, ino_n, &val);

	free(mem);

	return rc;
}

int dbfs_dir_write(DB_TXN *txn, guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return gfs->meta->put(gfs->meta, txn, &key, val, 0) ? -EIO : 0;
}

