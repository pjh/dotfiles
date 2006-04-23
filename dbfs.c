/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2006  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#define FUSE_USE_VERSION 25

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>

enum {
	INO_SIZE		= 128,
};

typedef struct {
	char buf[INO_SIZE];
} dfs_ino_t;

struct ndb_val {
	void *data;
	unsigned int len;
};

static void ndb_free(struct ndb_val *val)
{
	/* TODO */
}

static int ndb_lookup(const char *path, struct ndb_val **val)
{
	/* TODO */
	*val = NULL;
	return -ENOMEM;
}

static int ndb_lookup_data(const char *path, size_t size, off_t offset,
			   struct ndb_val **val)
{
	/* TODO */
	*val = NULL;
	return -ENOMEM;
}

static int dfs_fill_stat(struct stat *stbuf, struct ndb_val *val)
{
	/* TODO */
	return -EIO;
}

static int dfs_fill_dir(fuse_fill_dir_t filler, struct ndb_val *val)
{
	/* TODO */
	return -EIO;
}

static int dfs_getattr(const char *path, struct stat *stbuf)
{
	int rc = -ENOENT;
	struct ndb_val *val = NULL;
	char *nspath;

	memset(stbuf, 0, sizeof(struct stat));

	nspath = g_strdup_printf("/meta/%s", path);
	rc = ndb_lookup(nspath, &val);
	if (rc)
		goto out;

	rc = dfs_fill_stat(stbuf, val);

	ndb_free(val);

out:
	g_free(nspath);
	return rc;
}

static int dfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	struct ndb_val *val = NULL;
	char *nspath;
	int rc;

	nspath = g_strdup_printf("/dir/%s", path);
	rc = ndb_lookup(nspath, &val);
	if (rc) {
		if (rc == -ENOENT)
			rc = -ENOTDIR;
		goto out;
	}

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	rc = dfs_fill_dir(filler, val);

	ndb_free(val);

out:
	g_free(nspath);
	return rc;
}

static int dfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	struct ndb_val *val = NULL;
	int rc;

	rc = ndb_lookup_data(path, size, offset, &val);
	if (rc)
		return rc;

	ndb_free(val);

	return 0;
}

static const struct fuse_operations dfs_ops = {
	.getattr	= dfs_getattr,
	.read		= dfs_read,
	.readdir	= dfs_readdir,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &dfs_ops);
}
