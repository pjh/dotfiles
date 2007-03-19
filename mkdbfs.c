
/*
 * Maintained by Jeff Garzik <jgarzik@pobox.com>
 *
 * Copyright 2006-2007 Red Hat, Inc.
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

static int make_root_dir(DB_TXN *txn)
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

	rc = dbfs_inode_write(txn, ino);
	if (rc)
		goto err_die;

	rc = dbfs_dir_new(txn, 1, 1, ino);
	if (rc)
		goto err_die;

	dbfs_inode_free(ino);
	return 0;

err_die:
	errno = -rc;
	perror("dbfs_inode_write");
	return 1;
}

int main (int argc, char *argv[])
{
	struct dbfs *fs = dbfs_new();
	DB_TXN *txn;
	int pgm_rc = 0;

	gfs = fs;

	if (!fs)
		return 1;

	int rc = dbfs_open(fs, DB_CREATE, DB_CREATE, "mkdbfs", FALSE);
	if (rc) {
		perror("mkdbfs open");
		return 1;
	}

	rc = fs->env->txn_begin(fs->env, NULL, &txn, 0);
	if (rc) {
		perror("mkdbfs txn_begin");
		pgm_rc = 1;
		goto out;
	}

	rc = make_root_dir(txn);
	if (rc) {
		txn->abort(txn);
		pgm_rc = 1;
		goto out;
	}

	rc = txn->commit(txn, 0);
	if (rc) {
		perror("mkdbfs txn_commit");
		pgm_rc = 1;
		goto out;
	}

out:
	dbfs_close(fs);
	return pgm_rc;
}

