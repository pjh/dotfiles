
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

#include <db.h>
#include "dbfs.h"

int main (int argc, char *argv[])
{
	struct dbfs *fs;
	int rc;

	fs = dbfs_new();

	gfs = fs;

	rc = dbfs_open(fs, DB_RECOVER_FATAL, DB_CREATE, "dbfsck");
	if (rc) {
		perror("dbfsck");
		return 1;
	}

	dbfs_close(fs);
	return 0;
}

