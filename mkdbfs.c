
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs *gfs;

void create_db(void)
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

	rc = db_env_create(&gfs->env, 0);
	if (rc) {
		fprintf(stderr, "gfs->env_create failed: %d\n", rc);
		exit(1);
	}

	gfs->env->set_errfile(gfs->env, stderr);
	gfs->env->set_errpfx(gfs->env, "mkdbfs");

	if (db_password) {
		flags |= DB_ENCRYPT;
		rc = gfs->env->set_encrypt(gfs->env, db_password, DB_ENCRYPT_AES);
		if (rc) {
			gfs->env->err(gfs->env, rc, "gfs->env->set_encrypt");
			goto err_out;
		}

		/* FIXME: this isn't a very good way to shroud the password */
		if (putenv("DB_PASSWORD=X"))
			perror("putenv (SECURITY WARNING)");
	}

	/* init DB transactional environment, stored in directory db_home */
	rc = gfs->env->open(gfs->env, db_home,
			  DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL |
			  DB_INIT_TXN | DB_CREATE | flags, 0666);
	if (rc) {
		gfs->env->err(gfs->env, rc, "gfs->env->open");
		goto err_out;
	}

	/*
	 * Open metadata database
	 */

	rc = db_create(&gfs->meta, gfs->env, 0);
	if (rc) {
		gfs->env->err(gfs->env, rc, "db_create");
		goto err_out;
	}

	rc = gfs->meta->open(gfs->meta, NULL, "metadata", NULL,
			   DB_HASH,
			   DB_AUTO_COMMIT | DB_CREATE | DB_TRUNCATE | flags,
			   0666);
	if (rc) {
		gfs->meta->err(gfs->meta, rc, "gfs->meta->open");
		goto err_out_meta;
	}

	/* our data items are small, so use the smallest possible page
	 * size.  This is a guess, and should be verified by looking at
	 * overflow pages and other DB statistics.
	 */
	rc = gfs->meta->set_pagesize(gfs->meta, 512);
	if (rc) {
		gfs->meta->err(gfs->meta, rc, "gfs->meta->set_pagesize");
		goto err_out_meta;
	}

	/* fix everything as little endian */
	rc = gfs->meta->set_lorder(gfs->meta, 1234);
	if (rc) {
		gfs->meta->err(gfs->meta, rc, "gfs->meta->set_lorder");
		goto err_out_meta;
	}

	return;

err_out_meta:
	gfs->meta->close(gfs->meta, 0);
err_out:
	gfs->env->close(gfs->env, 0);
	exit(1);
}

static void make_root_dir(void)
{
	struct dbfs_inode *ino;
	guint64 curtime;
	int rc;

	/* allocate an empty inode */
	ino = g_new0(struct dbfs_inode, 1);
	ino->raw_ino_size = sizeof(struct dbfs_raw_inode);
	ino->raw_inode = malloc(ino->raw_ino_size);
	memset(ino->raw_inode, 0, ino->raw_ino_size);

	ino->raw_inode->ino = GUINT64_TO_LE(1);
	ino->raw_inode->mode = GUINT32_TO_LE(S_IFDIR | 0755);
	ino->raw_inode->nlink = GUINT32_TO_LE(2);
	curtime = GUINT64_TO_LE(time(NULL));
	ino->raw_inode->ctime = curtime;
	ino->raw_inode->atime = curtime;
	ino->raw_inode->mtime = curtime;

	rc = dbfs_inode_write(ino);
	if (rc)
		goto err_die;

	rc = dbfs_dir_new(1, 1, ino);
	if (rc)
		goto err_die;
	
	dbfs_inode_free(ino);
	return;

err_die:
	errno = -rc;
	perror("dbfs_inode_write");
	exit(1);
}

int main (int argc, char *argv[])
{
	gfs = dbfs_new();
	create_db();
	make_root_dir();
	dbfs_close(gfs);
	return 0;
}

