
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

static const char *hello_str = "Hello World!\n";

struct dbfs_inode {
	unsigned int		n_extents;
	struct dbfs_raw_inode	raw_inode;
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

static void dbfs_op_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct dbfs_dirent *de;
	DBT val;
	int rc;
	void *p;
	size_t namelen = strlen(name);

	rc = dbfs_read_dir(parent, &val);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	p = val.data;
	while (1) {
		de = p;
		de->magic	= GUINT32_FROM_LE(de->magic);
		de->namelen	= GUINT16_FROM_LE(de->namelen);
		de->ino		= GUINT64_FROM_LE(de->ino);

		g_assert (de->magic == DBFS_DE_MAGIC);
		if (!de->namelen)
			break;

		if ((namelen == de->namelen) &&
		    (!memcmp(name, de->name, namelen))) {
			struct fuse_entry_param e;

			memset(&e, 0, sizeof(e));

			e.ino = de->ino;
			e.attr_timeout = 1.0;
			e.entry_timeout = 1.0;

			fuse_reply_entry(req, &e);

			goto out;
		}

		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	fuse_reply_err(req, ENOENT);

out:
	free(val.data);
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

static void dbfs_op_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	struct dirbuf b;
	struct dbfs_dirent *de;
	void *p;
	char *s;

	memset(&b, 0, sizeof(b));

	p = (void *) (unsigned long) fi->fh;
	while (1) {
		de = p;
		de->magic	= GUINT32_FROM_LE(de->magic);
		de->namelen	= GUINT16_FROM_LE(de->namelen);
		de->ino		= GUINT64_FROM_LE(de->ino);

		g_assert (de->magic == DBFS_DE_MAGIC);
		if (!de->namelen)
			break;

		s = g_strndup(de->name, de->namelen);
		dirbuf_add(&b, s, de->ino);
		free(s);

		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	reply_buf_limited(req, b.p, b.size, off, size);
	free(b.p);
}

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

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void)fi;

	assert(ino == 2);
	reply_buf_limited(req, hello_str, strlen(hello_str), off, size);
}

static struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup		= dbfs_op_lookup,
	.getattr	= dbfs_op_getattr,
	.open		= hello_ll_open,
	.read		= hello_ll_read,
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

		se = fuse_lowlevel_new(&args, &hello_ll_oper,
				       sizeof(hello_ll_oper), NULL);
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
