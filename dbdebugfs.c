
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

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <db.h>
#include "dbfs.h"

#if 0
static guint64 cwd = DBFS_ROOT_INO;
#endif

static int op_cd(const char *path)
{
	return 1;	/* FIXME */
}

static void unknown_cmd(void)
{
	printf("error: unknown command\n");
}

static void print_help(void)
{

	printf(
"commands:\n"
"cd PATH	Change current working directory to PATH\n"
"exit		Exit program\n"
"help		Print summary of supported commands\n"
	);

}

static int main_loop(struct dbfs *fs)
{
	char buf[512], s[512];
	int rc = 0;

	while (1) {
		printf("dbdebugfs: ");
		fflush(stdout);

		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;

		/* trim trailing whitespace */
		while (buf[0] && (isspace(buf[strlen(buf) - 1])))
			buf[strlen(buf) - 1] = 0;

		if (!strcmp(buf, "help"))
			print_help();
		else if (!strcmp(buf, "exit"))
			break;
		else if (sscanf(buf, "cd %s", s) == 1)
			rc = op_cd(s);
		else
			unknown_cmd();

		if (rc)
			break;
	}

	return rc;
}

int main (int argc, char *argv[])
{
	struct dbfs *fs;
	int rc;

	fs = dbfs_new();

	gfs = fs;

	if (!fs)
		return 1;

	rc = dbfs_open(fs, DB_RECOVER | DB_CREATE, DB_CREATE,
		       "dbdebugfs", FALSE);
	if (rc) {
		perror("dbfsck");
		return 1;
	}

	rc = main_loop(fs);

	dbfs_close(fs);
	return rc;
}

