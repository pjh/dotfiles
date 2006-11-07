
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

#define FUSE_USE_VERSION 25

#define _BSD_SOURCE

#include <fuse_lowlevel.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/vfs.h>
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

static void dbfs_fill_attr(const struct dbfs_inode *ino, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	st->st_dev	= 1;
	st->st_ino	= GUINT64_FROM_LE(ino->raw_inode->ino);
	st->st_mode	= GUINT32_FROM_LE(ino->raw_inode->mode);
	st->st_nlink	= GUINT32_FROM_LE(ino->raw_inode->nlink);
	st->st_uid	= GUINT32_FROM_LE(ino->raw_inode->uid);
	st->st_gid	= GUINT32_FROM_LE(ino->raw_inode->gid);
	st->st_rdev	= GUINT64_FROM_LE(ino->raw_inode->rdev);
	st->st_size	= GUINT64_FROM_LE(ino->raw_inode->size);
	st->st_blksize	= 512;
	st->st_blocks	= GUINT64_FROM_LE(ino->raw_inode->size) / 512ULL;
	st->st_atime	= GUINT64_FROM_LE(ino->raw_inode->atime);
	st->st_mtime	= GUINT64_FROM_LE(ino->raw_inode->mtime);
	st->st_ctime	= GUINT64_FROM_LE(ino->raw_inode->ctime);
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
	struct dbfs *fs;
	int rc;

	fs = dbfs_new();

	rc = dbfs_open(fs, DB_RECOVER | DB_CREATE, DB_CREATE, "dbfs");
	if (rc)
		abort();			/* TODO: improve */

	gfs = fs;
}

static void dbfs_op_destroy(void *userdata)
{
	struct dbfs *fs = gfs;

	dbfs_close(fs);
	dbfs_free(fs);

	gfs = NULL;
}

static void dbfs_op_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	guint64 ino_n;
	struct dbfs_inode *ino;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	/* lookup inode in parent directory */
	rc = dbfs_dir_lookup(txn, parent, name, &ino_n);
	if (rc)
		goto err_out;

	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		goto err_out;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		fuse_reply_err(req, rc);
		return;
	}

	/* send reply */
	dbfs_reply_ino(req, ino);
	return;

err_out:
	txn->abort(txn);
	fuse_reply_err(req, -rc);
}

static void dbfs_op_getattr(fuse_req_t req, fuse_ino_t ino_n,
			     struct fuse_file_info *fi)
{
	struct dbfs_inode *ino;
	struct stat st;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		fuse_reply_err(req, rc);
		return;
	}

	/* read inode from database */
	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc) {
		rc = ENOENT;
		goto err_out_txn;
	}

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* fill in stat buf, taking care to convert from
	 * little endian to native endian
	 */
	dbfs_fill_attr(ino, &st);

	/* send result back to FUSE */
	fuse_reply_attr(req, &st, 2.0);

	dbfs_inode_free(ino);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, rc);
}

static void dbfs_op_setattr(fuse_req_t req, fuse_ino_t ino_n,
			    struct stat *attr, int to_set,
			    struct fuse_file_info *fi)
{
	struct dbfs_inode *ino;
	struct stat st;
	int rc, dirty = 0;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* read inode from database */
	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		goto err_out_txn;

	if (to_set & FUSE_SET_ATTR_MODE) {
		ino->raw_inode->mode = GUINT32_TO_LE(attr->st_mode);
		dirty = 1;
	}
	if (to_set & FUSE_SET_ATTR_UID) {
		ino->raw_inode->uid = GUINT32_TO_LE(attr->st_uid);
		dirty = 1;
	}
	if (to_set & FUSE_SET_ATTR_GID) {
		ino->raw_inode->gid = GUINT32_TO_LE(attr->st_gid);
		dirty = 1;
	}
	if (to_set & FUSE_SET_ATTR_SIZE) {
		rc = dbfs_inode_resize(txn, ino, attr->st_size);
		if (rc)
			goto err_out_free;
		ino->raw_inode->size = GUINT64_TO_LE(attr->st_size);
		dirty = 1;
	}
	if (to_set & FUSE_SET_ATTR_ATIME) {
		ino->raw_inode->atime = GUINT64_TO_LE(attr->st_atime);
		dirty = 1;
	}
	if (to_set & FUSE_SET_ATTR_MTIME) {
		ino->raw_inode->mtime = GUINT64_TO_LE(attr->st_mtime);
		dirty = 1;
	}

	if (dirty) {
		rc = dbfs_inode_write(txn, ino);
		if (rc)
			goto err_out_free;
	}

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		txn = NULL;
		goto err_out_free;
	}

	dbfs_fill_attr(ino, &st);
	dbfs_inode_free(ino);
	fuse_reply_attr(req, &st, 2.0);
	return;

err_out_free:
	dbfs_inode_free(ino);
err_out_txn:
	if (txn)
		txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_readlink(fuse_req_t req, fuse_ino_t ino)
{
	int rc;
	DBT val;
	char *s;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* read link from database */
	rc = dbfs_symlink_read(txn, ino, &val);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		free(val.data);
		goto err_out;
	}

	/* send reply; use g_strndup to append a trailing null */
	s = g_strndup(val.data, val.size);
	fuse_reply_readlink(req, s);
	g_free(s);

	free(val.data);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
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
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_mode_validate(mode);
	if (rc)
		goto err_out_txn;

	/* these have separate inode-creation hooks */
	if (S_ISDIR(mode) || S_ISLNK(mode)) {
		rc = -EINVAL;
		goto err_out_txn;
	}

	rc = dbfs_mknod(txn, parent, name, mode, rdev, &ino);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		rc = -rc;
		goto err_out;
	}

	dbfs_reply_ino(req, ino);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
			  mode_t mode)
{
	struct dbfs_inode *ino;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	mode &= ALLPERMS;
	mode |= S_IFDIR;

	rc = dbfs_mknod(txn, parent, name, mode, 0, &ino);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		rc = -rc;
		goto err_out;
	}

	dbfs_reply_ino(req, ino);

	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	rc = dbfs_unlink(txn, parent, name, 0);
	if (rc)
		goto err_out;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

out:
	fuse_reply_err(req, -rc);
	return;

err_out:
	txn->abort(txn);
	goto out;
}

static void dbfs_op_link(fuse_req_t req, fuse_ino_t ino_n, fuse_ino_t parent,
			 const char *newname)
{
	struct dbfs_inode *ino;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* read inode from database */
	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc) {
		rc = -ENOENT;
		goto err_out_txn;
	}

	/* attempt to create hard link */
	rc = dbfs_link(txn, ino, ino_n, parent, newname);
	if (rc)
		goto err_out_ino;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		rc = -rc;
		goto err_out;
	}

	dbfs_reply_ino(req, ino);
	return;

err_out_ino:
	dbfs_inode_free(ino);
err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_open(fuse_req_t req, fuse_ino_t ino,
			 struct fuse_file_info *fi)
{
	fi->direct_io = 0;
	fi->keep_cache = 1;
	fuse_reply_open(req, fi);
}

static void dbfs_op_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			 off_t off, struct fuse_file_info *fi)
{
	void *buf = NULL;
	int rc, rc2;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_read(txn, ino, off, size, &buf);
	if (rc < 0)
		goto err_out_txn;

	rc2 = txn->commit(txn, 0);
	if (rc2) {
		rc = -rc2;
		goto err_out;
	}

	fuse_reply_buf(req, buf, rc);
	free(buf);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
			  size_t size, off_t off, struct fuse_file_info *fi)
{
	int rc, rc2;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_write(txn, ino, off, buf, size);
	if (rc < 0)
		goto err_out_txn;

	rc2 = txn->commit(txn, 0);
	if (rc2) {
		rc = -rc2;
		goto err_out;
	}

	fuse_reply_write(req, rc);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
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
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	/* get inode number associated with name */
	rc = dbfs_dir_lookup(txn, parent, name, &ino_n);
	if (rc)
		goto out_txn;

	/* read dir associated with name */
	rc = dbfs_dir_read(txn, ino_n, &val);
	if (rc)
		goto out_txn;

	/* make sure dir only contains "." and ".." */
	rc = dbfs_dir_foreach(val.data, dbfs_chk_empty, NULL);
	free(val.data);

	/* if dbfs_chk_empty() returns non-zero, dir is not empty */
	if (rc)
		goto out_txn;

	/* dir is empty, go ahead and unlink */
	rc = dbfs_unlink(txn, parent, name, DBFS_UNLINK_DIR);
	if (rc)
		goto out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	fuse_reply_err(req, 0);
	return;

out_txn:
	txn->abort(txn);
out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_symlink(fuse_req_t req, const char *link,
			    fuse_ino_t parent, const char *name)
{
	struct dbfs_inode *ino;
	int rc;
	DB_TXN *txn;

	if (!g_utf8_validate(link, -1, NULL)) {
		rc = -EINVAL;
		goto err_out;
	}

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_mknod(txn, parent, name,
			S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO, 0, &ino);
	if (rc)
		goto err_out_txn;

	rc = dbfs_symlink_write(txn, GUINT64_FROM_LE(ino->raw_inode->ino), link);
	if (rc)
		goto err_out_ino;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		rc = -rc;
		goto err_out;
	}

	dbfs_reply_ino(req, ino);
	return;

err_out_ino:
	dbfs_inode_free(ino);
err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_rename(fuse_req_t req, fuse_ino_t parent,
			   const char *name, fuse_ino_t newparent,
			   const char *newname)
{
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	rc = dbfs_rename(txn, parent, name, newparent, newname);
	if (rc)
		goto out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	fuse_reply_err(req, 0);
	return;

out_txn:
	txn->abort(txn);
out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_fsync (fuse_req_t req, fuse_ino_t ino,
			   int datasync, struct fuse_file_info *fi)
{
	/* DB should have already sync'd our data for us */
	fuse_reply_err(req, 0);
}

static void dbfs_op_opendir(fuse_req_t req, fuse_ino_t ino,
			    struct fuse_file_info *fi)
{
	DBT val;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* read directory from database */
	rc = dbfs_dir_read(txn, ino, &val);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	/* save for later use */
	fi->fh = (uint64_t) (unsigned long) val.data;

	/* send reply */
	fuse_reply_open(req, fi);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
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

static void dbfs_op_releasedir(fuse_req_t req, fuse_ino_t ino,
			       struct fuse_file_info *fi)
{
	void *p = (void *) (unsigned long) fi->fh;

	/* release directory contents */
	free(p);
}

static void dbfs_op_fsyncdir (fuse_req_t req, fuse_ino_t ino,
			      int datasync, struct fuse_file_info *fi)
{
	/* DB should have already sync'd our data for us */
	fuse_reply_err(req, 0);
}

#define COPY(x) f.f_##x = st.f_##x
static void dbfs_op_statfs(fuse_req_t req)
{
	struct statvfs f;
	struct statfs st;
	
	if (statfs(gfs->home, &st) < 0) {
		fuse_reply_err(req, errno);
		return;
	}

	memset(&f, 0, sizeof(f));
	COPY(bsize);
	f.f_frsize = 512;
	COPY(blocks);
	COPY(bfree);
	COPY(bavail);
	f.f_files = 0xfffffff;
	f.f_ffree = 0xffffff;
	f.f_favail = 0xffffff;
	f.f_fsid = 0xdeadbeef;
	f.f_flag = 0;
	f.f_namemax = DBFS_FILENAME_MAX;

	fuse_reply_statfs(req, &f);
}
#undef COPY

static void dbfs_op_setxattr(fuse_req_t req, fuse_ino_t ino,
			     const char *name, const char *value,
			     size_t size, int flags)
{
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_xattr_set(txn, ino, name, value, size, flags);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	fuse_reply_err(req, 0);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_getxattr(fuse_req_t req, fuse_ino_t ino,
			     const char *name, size_t size)
{
	void *buf = NULL;
	size_t buflen = 0;
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_xattr_get(txn, ino, name, &buf, &buflen);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

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

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
	int rc;
	void *buf;
	size_t buflen;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_xattr_list(txn, ino, &buf, &buflen);
	if (rc < 0)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	if (size == 0)
		fuse_reply_xattr(req, buflen);
	else if (size < buflen)
		fuse_reply_err(req, ERANGE);
	else
		fuse_reply_buf(req, buf, buflen);
	
	free(buf);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_removexattr(fuse_req_t req, fuse_ino_t ino,
				const char *name)
{
	int rc;
	DB_TXN *txn;

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	rc = dbfs_xattr_remove(txn, ino, name, TRUE);
	if (rc)
		goto err_out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		rc = -rc;
		goto err_out;
	}

	fuse_reply_err(req, 0);
	return;

err_out_txn:
	txn->abort(txn);
err_out:
	fuse_reply_err(req, -rc);
}

static void dbfs_op_access(fuse_req_t req, fuse_ino_t ino_n, int mask)
{
	struct dbfs_inode *ino;
	const struct fuse_ctx *ctx;
	int rc;
	guint32 mode, uid, gid;
	DB_TXN *txn;

	ctx = fuse_req_ctx(req);
	g_assert(ctx != NULL);

	rc = gfs->env->txn_begin(gfs->env, NULL, &txn, 0);
	if (rc) {
		rc = -rc;
		goto out;
	}

	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		goto out_txn;

	rc = txn->commit(txn, 0);
	if (rc) {
		dbfs_inode_free(ino);
		rc = -rc;
		goto out;
	}

	mode = GUINT32_FROM_LE(ino->raw_inode->mode);
	uid = GUINT32_FROM_LE(ino->raw_inode->uid);
	gid = GUINT32_FROM_LE(ino->raw_inode->gid);

	if (uid == ctx->uid)
		mode >>= 8;
	else if (gid == ctx->gid)
		mode >>= 4;

	rc = 0;
	if ((mask & R_OK) && (!(mode & S_IROTH)))
		rc = -EACCES;
	if ((mask & W_OK) && (!(mode & S_IWOTH)))
		rc = -EACCES;
	if ((mask & X_OK) && (!(mode & S_IXOTH)))
		rc = -EACCES;

	dbfs_inode_free(ino);

out:
	fuse_reply_err(req, -rc);
	return;

out_txn:
	txn->abort(txn);
	goto out;
}

static struct fuse_lowlevel_ops dbfs_ops = {
	.init		= dbfs_op_init,
	.destroy	= dbfs_op_destroy,
	.lookup		= dbfs_op_lookup,
	.forget		= NULL,
	.getattr	= dbfs_op_getattr,
	.setattr	= dbfs_op_setattr,
	.readlink	= dbfs_op_readlink,
	.mknod		= dbfs_op_mknod,
	.mkdir		= dbfs_op_mkdir,
	.unlink		= dbfs_op_unlink,
	.rmdir		= dbfs_op_rmdir,
	.symlink	= dbfs_op_symlink,
	.rename		= dbfs_op_rename,
	.link		= dbfs_op_link,
	.open		= dbfs_op_open,
	.read		= dbfs_op_read,
	.write		= dbfs_op_write,
	.flush		= NULL,
	.release	= NULL,
	.fsync		= dbfs_op_fsync,
	.opendir	= dbfs_op_opendir,
	.readdir	= dbfs_op_readdir,
	.releasedir	= dbfs_op_releasedir,
	.fsyncdir	= dbfs_op_fsyncdir,
	.statfs		= dbfs_op_statfs,
	.setxattr	= dbfs_op_setxattr,
	.getxattr	= dbfs_op_getxattr,
	.listxattr	= dbfs_op_listxattr,
	.removexattr	= dbfs_op_removexattr,
	.access		= dbfs_op_access,
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
