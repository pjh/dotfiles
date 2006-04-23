
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

static gchar *pprefix(const char *pfx, const char *path_in)
{
	gchar *path, *s;

	path = g_strdup(path_in);
	while ((*path) && (path[strlen(path) - 1] == '/'))
		path[strlen(path) - 1] = 0;

	s = g_strdup_printf("%s%s", pfx, path);

	g_free(path);
	return s;
}

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

static int dfs_fill_data(char *buf, size_t size, off_t offset,
			 struct ndb_val *val)
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

	nspath = pprefix("/meta", path);
	rc = ndb_lookup(nspath, &val);
	if (rc)
		goto out;

	rc = dfs_fill_stat(stbuf, val);

	ndb_free(val);

out:
	g_free(nspath);
	return rc;
}

static int dfs_readlink(const char *path, char *buf, size_t size)
{
	int rc = -ENOENT;
	struct ndb_val *val = NULL;
	char *nspath;

	nspath = pprefix("/symlink", path);
	rc = ndb_lookup(nspath, &val);
	if (rc)
		goto out;

	memcpy(buf, val->data, size < val->len ? size : val->len);

	ndb_free(val);

out:
	g_free(nspath);
	return rc;
}

static int dfs_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	struct ndb_val *val = NULL;
	char *nspath;
	int rc = 0;

	nspath = pprefix("/data", path);
	rc = ndb_lookup(path, &val);
	if (rc)
		goto out;

	rc = dfs_fill_data(buf, size, offset, val);

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

	nspath = pprefix("/dir", path);
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

static const struct fuse_operations dfs_ops = {
	.getattr	= dfs_getattr,
	.readlink	= dfs_readlink,
	.read		= dfs_read,
	.readdir	= dfs_readdir,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &dfs_ops);
}
