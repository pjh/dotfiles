
#include <db.h>
#include "dbfs.h"

int main (int argc, char *argv[])
{
	struct dbfs *fs;
	int rc;

	fs = dbfs_new();

	rc = dbfs_open(fs, DB_RECOVER, 0, "dbfsck");
	if (rc) {
		perror("dbfsck");
		return 1;
	}

	dbfs_close(fs);
	return 0;
}

