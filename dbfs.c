
#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs_inode {
	unsigned int		n_extents;
	struct dbfs_raw_inode	raw_inode;
};

struct dbfs_lookup_info {
	const char	*name;
	size_t		namelen;
	guint64		*ino;
};

static DB_ENV *env;
static DB *db_data;
static DB *db_meta;

static int dbfs_read_inode(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_raw_inode *raw_ino;
	struct dbfs_inode *ino;
	size_t ex_sz, i;

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	key.data = key_str;
	key.size = strlen(key_str);

	val.flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, &val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return rc;

	raw_ino = val.data;
	raw_ino->ino		= GUINT64_FROM_LE(raw_ino->ino);
	raw_ino->generation	= GUINT64_FROM_LE(raw_ino->generation);
	raw_ino->mode		= GUINT32_FROM_LE(raw_ino->mode);
	raw_ino->nlink		= GUINT32_FROM_LE(raw_ino->nlink);
	raw_ino->uid		= GUINT32_FROM_LE(raw_ino->uid);
	raw_ino->gid		= GUINT32_FROM_LE(raw_ino->gid);
	raw_ino->rdev		= GUINT64_FROM_LE(raw_ino->rdev);
	raw_ino->size		= GUINT64_FROM_LE(raw_ino->size);
	raw_ino->ctime		= GUINT64_FROM_LE(raw_ino->ctime);
	raw_ino->atime		= GUINT64_FROM_LE(raw_ino->atime);
	raw_ino->mtime		= GUINT64_FROM_LE(raw_ino->mtime);

	ex_sz = val.size - sizeof(struct dbfs_raw_inode);
	i = sizeof(struct dbfs_inode) - sizeof(struct dbfs_raw_inode);

	ino = g_malloc(i + ex_sz + sizeof(struct dbfs_raw_inode));
	memcpy(&ino->raw_inode, raw_ino, val.size);
	ino->n_extents = ex_sz / sizeof(struct dbfs_extent);

	*ino_out = ino;

	free(val.data);

	return 0;
}

static int dbfs_read_dir(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];
	int rc;

	memset(&key, 0, sizeof(key));
	memset(val, 0, sizeof(*val));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	val->flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOTDIR;
	return rc;
}

static int dbfs_read_link(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];
	int rc;

	memset(&key, 0, sizeof(key));
	memset(val, 0, sizeof(*val));

	sprintf(key_str, "/symlink/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	val->flags = DB_DBT_MALLOC;

	rc = db_meta->get(db_meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc;
}

typedef int (*dbfs_dir_actor_t) (struct dbfs_dirent *, void *);

static int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata)
{
	struct dbfs_dirent *de;
	void *p;
	int rc = 0;

	p = dir;
	while (1) {
		de = p;
		de->magic	= GUINT32_FROM_LE(de->magic);
		de->namelen	= GUINT16_FROM_LE(de->namelen);
		de->ino		= GUINT64_FROM_LE(de->ino);

		g_assert (de->magic == DBFS_DE_MAGIC);
		if (!de->namelen)
			break;

		rc = func(de, userdata);
		if (rc)
			break;

		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	return rc;
}

static int dbfs_dir_cmp(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_lookup_info *li = userdata;

	if ((li->namelen == de->namelen) &&
	    (!memcmp(li->name, de->name, li->namelen))) {
	    	*li->ino = de->ino;
		return 1;
	}

	return 0;
}

static int dbfs_lookup(guint64 parent, const char *name, guint64 *ino)
{
	struct dbfs_lookup_info li;
	size_t namelen = strlen(name);
	DBT val;
	int rc;

	*ino = 0;

	rc = dbfs_read_dir(parent, &val);
	if (rc)
		return rc;

	li.name = name;
	li.namelen = namelen;
	li.ino = ino;
	rc = dbfs_dir_foreach(val.data, dbfs_dir_cmp, &li);
	if (rc)
		rc = 0;
	else
		rc = -ENOENT;

	free(val.data);
	return rc;
}

static int dbfs_unlink(guint64 parent, const char *name)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc;

	rc = dbfs_lookup(parent, name, &ino_n);
	if (rc)
		goto err_out;

	rc = dbfs_read_inode(ino_n, &ino);
	if (rc)
		goto err_out;

	/* FIXME stopped working here...

	 * delete dir entry
	 * decrement n_links
	 * if n_links==0, delete inode
	 */

err_out:
	return 0; /* FIXME */
}

static void dbfs_op_getattr(fuse_req_t req, fuse_ino_t ino_n,
			     struct fuse_file_info *fi)
{
	struct dbfs_inode *ino = NULL;
	struct stat st;
	int rc;

	rc = dbfs_read_inode(ino_n, &ino);
	if (rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	memset(&st, 0, sizeof(st));
	st.st_dev	= 1;
	st.st_ino	= ino_n;
	st.st_mode	= ino->raw_inode.mode;
	st.st_nlink	= ino->raw_inode.nlink;
	st.st_uid	= ino->raw_inode.uid;
	st.st_gid	= ino->raw_inode.gid;
	st.st_rdev	= ino->raw_inode.rdev;
	st.st_size	= ino->raw_inode.size;
	st.st_blksize	= 512;
	st.st_blocks	= ino->raw_inode.size / 512;
	st.st_atime	= ino->raw_inode.atime;
	st.st_mtime	= ino->raw_inode.mtime;
	st.st_ctime	= ino->raw_inode.ctime;

	fuse_reply_attr(req, &st, 1.0);

	g_free(ino);
}

static void dbfs_op_readlink(fuse_req_t req, fuse_ino_t ino)
{
	int rc;
	DBT val;
	char *s;

	rc = dbfs_read_link(ino, &val);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	s = g_strndup(val.data, val.size);
	fuse_reply_readlink(req, s);
	g_free(s);

	free(val.data);
}

static void dbfs_op_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;
	guint64 ino;
	int rc;

	rc = dbfs_lookup(parent, name, &ino);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	memset(&e, 0, sizeof(e));

	e.ino = ino;
	e.attr_timeout = 2.0;
	e.entry_timeout = 2.0;

	fuse_reply_entry(req, &e);
}

static void dbfs_op_opendir(fuse_req_t req, fuse_ino_t ino,
			    struct fuse_file_info *fi)
{
	DBT val;
	int rc;

	rc = dbfs_read_dir(ino, &val);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	fi->fh = (uint64_t) (unsigned long) val.data;

	fuse_reply_open(req, fi);
}

static void dbfs_op_releasedir(fuse_req_t req, fuse_ino_t ino,
			       struct fuse_file_info *fi)
{
	void *p = (void *) (unsigned long) fi->fh;
	free(p);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(struct dirbuf *b, const char *name, fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_dirent_size(strlen(name));
	b->p = (char *)realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_dirent(b->p + oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static int dbfs_fill_dirbuf(struct dbfs_dirent *de, void *userdata)
{
	struct dirbuf *b = userdata;
	char *s = g_strndup(de->name, de->namelen);
	dirbuf_add(b, s, de->ino);
	free(s);
	return 0;
}

static void dbfs_op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	struct dirbuf b;
	void *p;

	p = (void *) (unsigned long) fi->fh;

	memset(&b, 0, sizeof(b));
	dbfs_dir_foreach(p, dbfs_fill_dirbuf, &b);

	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

static int dbfs_chk_empty(struct dbfs_dirent *de, void *userdata)
{
	if ((de->namelen == 1) && (!memcmp(de->name, ".", 1)))
		return 0;
	if ((de->namelen == 2) && (!memcmp(de->name, "..", 2)))
		return 0;
	return ENOTEMPTY;
}

static void dbfs_op_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	int rc = dbfs_unlink(parent, name);
	if (rc)
		fuse_reply_err(req, rc);
}

static void dbfs_op_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc;
	DBT val;

	rc = dbfs_lookup(parent, name, &ino_n);
	if (rc)
		goto err_out;

	rc = dbfs_read_inode(ino_n, &ino);
	if (rc)
		goto err_out;
	if (!S_ISDIR(ino->raw_inode.mode)) {
		rc = ENOTDIR;
		goto err_out_free;
	}

	rc = dbfs_read_dir(parent, &val);
	if (rc)
		goto err_out_free;

	rc = dbfs_dir_foreach(val.data, dbfs_chk_empty, NULL);
	free(val.data);

	if (rc)
		goto err_out_free;

	rc = dbfs_unlink(parent, name);
	if (rc)
		goto err_out_free;

	return;

err_out_free:
	g_free(ino);
err_out:
	fuse_reply_err(req, rc);
}

#if 0
static void hello_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static const char *hello_str = "Hello World!\n";

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void)fi;

	assert(ino == 2);
	reply_buf_limited(req, hello_str, strlen(hello_str), off, size);
}
#endif

static struct fuse_lowlevel_ops dbfs_ops = {
	.lookup		= dbfs_op_lookup,
	.getattr	= dbfs_op_getattr,
	.readlink	= dbfs_op_readlink,
	.unlink		= dbfs_op_unlink,
	.rmdir		= dbfs_op_rmdir,
	.opendir	= dbfs_op_opendir,
	.readdir	= dbfs_op_readdir,
	.releasedir	= dbfs_op_releasedir,
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	char *mountpoint;
	int err = -1;
	int fd;

	(void) env;
	(void) db_data;

	if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
	    (fd = fuse_mount(mountpoint, &args)) != -1) {
		struct fuse_session *se;

		se = fuse_lowlevel_new(&args, &dbfs_ops,
				       sizeof(dbfs_ops), NULL);
		if (se != NULL) {
			if (fuse_set_signal_handlers(se) != -1) {
				struct fuse_chan *ch = fuse_kern_chan_new(fd);
				if (ch != NULL) {
					fuse_session_add_chan(se, ch);
					err = fuse_session_loop(se);
				}
				fuse_remove_signal_handlers(se);
			}
			fuse_session_destroy(se);
		}
		close(fd);
	}
	fuse_unmount(mountpoint);
	fuse_opt_free_args(&args);

	return err ? 1 : 0;
}
