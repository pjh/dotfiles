
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

static void dbfs_op_getattr(fuse_req_t req, fuse_ino_t ino_n,
			     struct fuse_file_info *fi)
{
	struct dbfs_inode *ino = NULL;
	struct stat st;
	int rc;

	/* read inode from database */
	rc = dbfs_inode_read(ino_n, &ino);
	if (rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	/* fill in stat buf, taking care to convert from
	 * little endian to native endian
	 */
	memset(&st, 0, sizeof(st));
	st.st_dev	= 1;
	st.st_ino	= ino_n;
	st.st_mode	= GUINT32_FROM_LE(ino->raw_inode->mode);
	st.st_nlink	= GUINT32_FROM_LE(ino->raw_inode->nlink);
	st.st_uid	= GUINT32_FROM_LE(ino->raw_inode->uid);
	st.st_gid	= GUINT32_FROM_LE(ino->raw_inode->gid);
	st.st_rdev	= GUINT64_FROM_LE(ino->raw_inode->rdev);
	st.st_size	= GUINT64_FROM_LE(ino->raw_inode->size);
	st.st_blksize	= 512;
	st.st_blocks	= GUINT64_FROM_LE(ino->raw_inode->size) / 512ULL;
	st.st_atime	= GUINT64_FROM_LE(ino->raw_inode->atime);
	st.st_mtime	= GUINT64_FROM_LE(ino->raw_inode->mtime);
	st.st_ctime	= GUINT64_FROM_LE(ino->raw_inode->ctime);

	/* send result back to FUSE */
	fuse_reply_attr(req, &st, 2.0);

	dbfs_inode_free(ino);
}

static void dbfs_op_readlink(fuse_req_t req, fuse_ino_t ino)
{
	int rc;
	DBT val;
	char *s;

	/* read link from database */
	rc = dbfs_read_link(ino, &val);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	/* send reply; use g_strndup to append a trailing null */
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

	/* lookup inode in parent directory */
	rc = dbfs_lookup(parent, name, &ino);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	/* send reply; timeout of 2.0 is just a guess */

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

	/* read directory from database */
	rc = dbfs_read_dir(ino, &val);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	/* save for later use */
	fi->fh = (uint64_t) (unsigned long) val.data;

	/* send reply */
	fuse_reply_open(req, fi);
}

static void dbfs_op_releasedir(fuse_req_t req, fuse_ino_t ino,
			       struct fuse_file_info *fi)
{
	void *p = (void *) (unsigned long) fi->fh;

	/* release directory contents */
	free(p);
}

struct dirbuf {
	char *p;
	size_t size;
};

/* stock function copied from FUSE template */
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

#ifndef min
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

/* stock function copied from FUSE template */
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
	char *s;

	/* add dirent to buffer; use g_strndup solely to append nul */
	s = g_strndup(de->name, de->namelen);
	dirbuf_add(b, s, de->ino);
	free(s);
	return 0;
}

static void dbfs_op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	struct dirbuf b;
	void *p;

	/* grab directory contents stored by opendir */
	p = (void *) (unsigned long) fi->fh;

	/* iterate through each dirent, filling dirbuf */
	memset(&b, 0, sizeof(b));
	dbfs_dir_foreach(p, dbfs_fill_dirbuf, &b);

	/* send reply */
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
	int rc = dbfs_unlink(parent, name, 0);
	if (rc)
		fuse_reply_err(req, rc);
}

static void dbfs_op_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	guint64 ino_n;
	int rc;
	DBT val;

	/* get inode number associated with name */
	rc = dbfs_lookup(parent, name, &ino_n);
	if (rc)
		goto err_out;

	/* read dir associated with name */
	rc = dbfs_read_dir(ino_n, &val);
	if (rc)
		goto err_out;

	/* make sure dir only contains "." and ".." */
	rc = dbfs_dir_foreach(val.data, dbfs_chk_empty, NULL);
	free(val.data);

	/* if dbfs_chk_empty() returns non-zero, dir is not empty */
	if (rc)
		goto err_out;

	/* dir is empty, go ahead and unlink */
	rc = dbfs_unlink(parent, name, DBFS_UNLINK_DIR);
	if (rc)
		goto err_out;

	return;

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
	.init		= dbfs_init,
	.destroy	= dbfs_exit,
	.lookup		= dbfs_op_lookup,
	.forget		= NULL,
	.getattr	= dbfs_op_getattr,
	.setattr	= NULL,
	.readlink	= dbfs_op_readlink,
	.mknod		= NULL,
	.mkdir		= NULL,
	.unlink		= dbfs_op_unlink,
	.rmdir		= dbfs_op_rmdir,
	.symlink	= NULL,
	.rename		= NULL,
	.link		= NULL,
	.open		= NULL,
	.read		= NULL,
	.write		= NULL,
	.flush		= NULL,
	.release	= NULL,
	.fsync		= NULL,
	.opendir	= dbfs_op_opendir,
	.readdir	= dbfs_op_readdir,
	.releasedir	= dbfs_op_releasedir,
	.fsyncdir	= NULL,
	.statfs		= NULL,
	.setxattr	= NULL,
	.getxattr	= NULL,
	.listxattr	= NULL,
	.removexattr	= NULL,
	.access		= NULL,
	.create		= NULL,
};

/* stock main() from FUSE example */
int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	char *mountpoint;
	int err = -1;
	int fd;

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
