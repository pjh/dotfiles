
/*
 * Copyright 2006 Jeff Garzik <jgarzik@pobox.com>
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
	struct dbfs *fs = dbfs_new();

	gfs = fs;

	if (!fs)
		return 1;

	int rc = dbfs_open(fs, DB_CREATE, DB_CREATE, "mkdbfs");
	if (rc) {
		perror("mkdbfs");
		return 1;
	}

	make_root_dir();
	dbfs_close(fs);
	return 0;
}

