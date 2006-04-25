
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

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
			  DB_INIT_TXN | DB_CREATE | flags, 0666);
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
			   DB_HASH,
			   DB_AUTO_COMMIT | DB_CREATE | DB_TRUNCATE | flags,
			   0666);
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

int main (int argc, char *argv[])
{
	dbfs_init(NULL);
	dbfs_exit(NULL);
	return 0;
}

