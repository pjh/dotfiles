
#define FUSE_USE_VERSION 25

#include <fuse_lowlevel.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

static void dbfs_fill_ent(const struct dbfs_inode *ino,
			  struct fuse_entry_param *ent)
{
	memset(ent, 0, sizeof(*ent));

	ent->ino = GUINT64_FROM_LE(ino->raw_inode->ino);
	ent->generation = GUINT64_FROM_LE(ino->raw_inode->version);

	/* these timeouts are just a guess */
	ent->attr_timeout = 2.0;
	ent->entry_timeout = 2.0;
}

static void dbfs_reply_ino(fuse_req_t req, struct dbfs_inode *ino)
{
	struct fuse_entry_param ent;

	dbfs_fill_ent(ino, &ent);

	fuse_reply_entry(req, &ent);

	dbfs_inode_free(ino);
}

static void dbfs_op_init(void *userdata)
{
	struct dbfs **fs_io = userdata;
	struct dbfs *fs;
	int rc;

	fs = dbfs_new();

	rc = dbfs_open(fs);
	if (rc)
		abort();			/* TODO: improve */

	*fs_io = fs;
}

static void dbfs_op_destroy(void *userdata)
{
	struct dbfs **fs_io = userdata;
	struct dbfs *fs = *fs_io;

	dbfs_close(fs);
	dbfs_free(fs);

	*fs_io = NULL;
}

static void dbfs_op_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	guint64 ino_n;
	struct dbfs_inode *ino;
	int rc;

	/* lookup inode in parent directory */
	rc = dbfs_dir_lookup(parent, name, &ino_n);
	if (rc) {
		fuse_reply_err(req, -rc);
		return;
	}

	rc = dbfs_inode_read(ino_n, &ino);
	if (rc) {
		fuse_reply_err(req, -rc);
		return;
	}

	/* send reply */
	dbfs_reply_ino(req, ino);
}

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
	rc = dbfs_symlink_read(ino, &val);
	if (rc) {
		fuse_reply_err(req, -rc);
		return;
	}

	/* send reply; use g_strndup to append a trailing null */
	s = g_strndup(val.data, val.size);
	fuse_reply_readlink(req, s);
	g_free(s);

	free(val.data);
}

static int dbfs_mode_validate(mode_t mode)
{
	unsigned int ifmt = mode & S_IFMT;
	int rc = 0;

	if (S_ISREG(mode)) {
		if (ifmt & ~S_IFREG)
			rc = -EINVAL;
	}
	else if (S_ISDIR(mode)) {
		if (ifmt & ~S_IFDIR)
			rc = -EINVAL;
	}
	else if (S_ISCHR(mode)) {
		if (ifmt & ~S_IFCHR)
			rc = -EINVAL;
	}
	else if (S_ISBLK(mode)) {
		if (ifmt & ~S_IFBLK)
			rc = -EINVAL;
	}
	else if (S_ISFIFO(mode)) {
		if (ifmt & ~S_IFIFO)
			rc = -EINVAL;
	}
	else if (S_ISLNK(mode))
		rc = -EINVAL;
	else if (S_ISSOCK(mode)) {
		if (ifmt & ~S_IFSOCK)
			rc = -EINVAL;
	}
	else
		rc = -EINVAL;

	return rc;
}

static void dbfs_op_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
			  mode_t mode, dev_t rdev)
{
	struct dbfs_inode *ino;
	int rc;

	rc = dbfs_mode_validate(mode);
	if (rc) {
		fuse_reply_err(req, -rc);
		return;
	}

	rc = dbfs_mknod(parent, name, mode, rdev, &ino);
	if (rc) {
		fuse_reply_err(req, -rc);
		return;
	}

	dbfs_reply_ino(req, ino);
}

static void dbfs_op_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
			  mode_t mode)
{
	mode &= (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX);
	mode |= S_IFDIR;

	return dbfs_op_mknod(req, parent, name, mode, 0);
}

static void dbfs_op_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	int rc = dbfs_unlink(parent, name, 0);
	fuse_reply_err(req, -rc);
}

static void dbfs_op_link(fuse_req_t req, fuse_ino_t ino_n, fuse_ino_t parent,
			 const char *newname)
{
	struct dbfs_inode *ino;
	int rc;

	/* read inode from database */
	rc = dbfs_inode_read(ino_n, &ino);
	if (rc) {
		fuse_reply_err(req, ENOENT);
		return;
	}

	/* attempt to create hard link */
	rc = dbfs_link(ino, ino_n, parent, newname);
	if (rc)
		goto err_out;

	dbfs_reply_ino(req, ino);
	return;

err_out:
	dbfs_inode_free(ino);
	fuse_reply_err(req, -rc);
}

static int dbfs_chk_empty(struct dbfs_dirent *de, void *userdata)
{
	if ((GUINT16_FROM_LE(de->namelen) == 1) && (!memcmp(de->name, ".", 1)))
		return 0;
	if ((GUINT16_FROM_LE(de->namelen) == 2) && (!memcmp(de->name, "..", 2)))
		return 0;
	return ENOTEMPTY;
}

static void dbfs_op_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	guint64 ino_n;
	int rc;
	DBT val;

	/* get inode number associated with name */
	rc = dbfs_dir_lookup(parent, name, &ino_n);
	if (rc)
		goto out;

	/* read dir associated with name */
	rc = dbfs_dir_read(ino_n, &val);
	if (rc)
		goto out;

	/* make sure dir only contains "." and ".." */
	rc = dbfs_dir_foreach(val.data, dbfs_chk_empty, NULL);
	free(val.data);

	/* if dbfs_chk_empty() returns non-zero, dir is not empty */
	if (rc)
		goto out;

	/* dir is empty, go ahead and unlink */
	rc = dbfs_unlink(parent, name, DBFS_UNLINK_DIR);

out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_symlink(fuse_req_t req, const char *link,
			    fuse_ino_t parent, const char *name)
{
	struct dbfs_inode *ino;
	int rc;

	if (!g_utf8_validate(link, -1, NULL)) {
		rc = -EINVAL;
		goto err_out;
	}

	rc = dbfs_mknod(parent, name,
			S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO, 0, &ino);
	if (rc)
		goto err_out;

	rc = dbfs_symlink_write(GUINT64_FROM_LE(ino->raw_inode->ino), link);
	if (rc)
		goto err_out_mknod;

	dbfs_reply_ino(req, ino);
	return;

err_out_mknod:
	dbfs_inode_del(ino);
	dbfs_inode_free(ino);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_opendir(fuse_req_t req, fuse_ino_t ino,
			    struct fuse_file_info *fi)
{
	DBT val;
	int rc;

	/* read directory from database */
	rc = dbfs_dir_read(ino, &val);
	if (rc) {
		fuse_reply_err(req, -rc);
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
static int reply_buf_limited(fuse_req_t req, const void *buf, size_t bufsize,
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
	s = g_strndup(de->name, GUINT16_FROM_LE(de->namelen));
	dirbuf_add(b, s, GUINT64_FROM_LE(de->ino));
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

static void dbfs_op_setxattr(fuse_req_t req, fuse_ino_t ino,
			     const char *name, const char *value,
			     size_t size, int flags)
{
	int rc = dbfs_xattr_set(ino, name, value, size, flags);
	fuse_reply_err(req, -rc);
}

static void dbfs_op_getxattr(fuse_req_t req, fuse_ino_t ino,
			     const char *name, size_t size)
{
	void *buf = NULL;
	size_t buflen = 0;
	int rc;

	rc = dbfs_xattr_get(ino, name, &buf, &buflen);
	if (rc)
		goto err_out;

	if (size == 0)
		fuse_reply_xattr(req, buflen);
	else if (buflen <= size)
		fuse_reply_buf(req, buf, buflen);
	else {
		rc = -ERANGE;
		goto err_out;
	}

	free(buf);
	return;

err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
	int rc;
	void *buf;
	size_t buflen;

	rc = dbfs_xattr_list(ino, &buf, &buflen);
	if (rc < 0) {
		fuse_reply_err(req, -rc);
		return;
	}

	if (size == 0)
		fuse_reply_xattr(req, buflen);
	else if (size < buflen)
		fuse_reply_err(req, ERANGE);
	else
		fuse_reply_buf(req, buf, buflen);
	
	free(buf);
}

static void dbfs_op_removexattr(fuse_req_t req, fuse_ino_t ino,
				const char *name)
{
	int rc = dbfs_xattr_remove(ino, name, TRUE);
	fuse_reply_err(req, -rc);
}

static struct fuse_lowlevel_ops dbfs_ops = {
	.init		= dbfs_op_init,
	.destroy	= dbfs_op_destroy,
	.lookup		= dbfs_op_lookup,
	.forget		= NULL,
	.getattr	= dbfs_op_getattr,
	.setattr	= NULL,
	.readlink	= dbfs_op_readlink,
	.mknod		= dbfs_op_mknod,
	.mkdir		= dbfs_op_mkdir,
	.unlink		= dbfs_op_unlink,
	.rmdir		= dbfs_op_rmdir,
	.symlink	= dbfs_op_symlink,
	.rename		= NULL,
	.link		= dbfs_op_link,
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
	.setxattr	= dbfs_op_setxattr,
	.getxattr	= dbfs_op_getxattr,
	.listxattr	= dbfs_op_listxattr,
	.removexattr	= dbfs_op_removexattr,
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
