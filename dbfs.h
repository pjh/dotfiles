#ifndef __DBFS_H__
#define __DBFS_H__

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

#include <glib.h>

#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

enum {
	DBFS_BLK_ID_LEN		= 20,		/* block id (sha1 hash) len */

	DBFS_UNLINK_DIR		= (1 << 0),

	DBFS_ROOT_INO		= 1,		/* root inode number */

	DBFS_DIRENT_ALIGN	= 8,	/* struct dbfs_dirent alignment */

	DBFS_XATTR_NAME_LEN	= 256,	/* extended attr limits */
	DBFS_XATTR_MAX_LEN	= (1024 * 1024),

	DBFS_XLIST_ALIGN	= 8,	/* struct dbfs_xlist alignment */

	/* our data items are small, so use the smallest possible page
	 * size.  This is a guess, and should be verified by looking at
	 * overflow pages and other DB statistics.
	 */
	DBFS_PGSZ_METADATA	= 512,

	/* another guess.  must take into account small data items
	 * as well as large ones
	 */
	DBFS_PGSZ_DATA		= 2048,

	DBFS_MAX_EXT_LEN	= (4 * 1024 * 1024),	/* max extent len */

	DBFS_ZERO_CMP_BLK_SZ	= 8192,
};

enum {
	/* dirent magic number */
	DBFS_DE_MAGIC		= 0xd4d4d4d4U,
};

enum dbfs_inode_type {
	IT_REG,
	IT_DIR,
	IT_DEV,
	IT_FIFO,
	IT_SYMLINK,
	IT_SOCKET
};

typedef struct {
	char		buf[DBFS_BLK_ID_LEN];
} dbfs_blk_id_t;

struct dbfs_dirent {
	guint32		magic;
	guint16		res2;
	guint16		namelen;
	guint64		ino;
	char		name[0];
} __attribute__ ((packed));

struct dbfs_xlist {
	guint32		namelen;
	char		name[0];
} __attribute__ ((packed));

struct dbfs_extent {
	guint32			off;		/* offset into block */
	guint32			len;		/* length of fragment */
	dbfs_blk_id_t		id;		/* block id */
} __attribute__ ((packed));

struct dbfs_hashref {
	guint32			refs;
} __attribute__ ((packed));

struct dbfs_raw_inode {
	guint64		ino;
	guint64		version;
	guint32		mode;
	guint32		nlink;
	guint32		uid;
	guint32		gid;
	guint64		rdev;
	guint64		size;
	guint64		ctime;
	guint64		atime;
	guint64		mtime;
	struct dbfs_extent blocks[0];
} __attribute__ ((packed));

struct dbfs_inode {
	unsigned int		n_extents;
	unsigned int		raw_ino_size;
	enum dbfs_inode_type	type;
	struct dbfs_raw_inode	*raw_inode;
};

struct dbfs {
	const char		*home;
	char			*passwd;

	DB_ENV			*env;
	DB			*meta;
	DB			*data;
	DB			*hashref;

	guint64			next_inode;
};


typedef int (*dbfs_dir_actor_t) (struct dbfs_dirent *, void *);

/* dbfs-backend.c */
extern int dbmeta_del(const char *key_str);
extern int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out);
extern int dbfs_dir_read(guint64 ino, DBT *val);
extern int dbfs_symlink_read(guint64 ino, DBT *val);
extern int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata);
extern int dbfs_dir_lookup(guint64 parent, const char *name, guint64 *ino);
extern int dbfs_link(struct dbfs_inode *ino, guint64 ino_n, guint64 parent, const char *name);
extern int dbfs_unlink(guint64 parent, const char *name, unsigned long flags);
extern void dbfs_init(void *userdata);
extern void dbfs_exit(void *userdata);
extern int dbfs_mknod(guint64 parent, const char *name,
		      guint32 mode, guint64 rdev,
		      struct dbfs_inode **ino);
extern int dbfs_symlink_write(guint64 ino, const char *link);
extern int dbfs_inode_del(struct dbfs_inode *ino);
extern int dbfs_xattr_get(guint64 ino_n, const char *name,
			  void **buf_out, size_t *buflen_out);
extern int dbfs_xattr_set(guint64 ino_n, const char *name,
			  const void *buf, size_t buflen,
			  int flags);
extern int dbfs_xattr_remove(guint64, const char *, gboolean);
extern int dbfs_xattr_list(guint64 ino, void **buf_out, size_t *buflen_out);
extern int dbfs_read(guint64, guint64, size_t, void **);

/* libdbfs.c */
extern int dbfs_open(struct dbfs *, unsigned int, unsigned int, const char *);
extern void dbfs_close(struct dbfs *fs);
extern struct dbfs *dbfs_new(void);
extern void dbfs_free(struct dbfs *fs);
extern struct dbfs *gfs;
extern int dbfs_inode_write(struct dbfs_inode *ino);
extern int dbfs_dir_new(guint64 parent, guint64 ino_n, const struct dbfs_inode *ino);
extern int dbfs_dir_write(guint64 ino, DBT *val);
extern void dbfs_inode_free(struct dbfs_inode *ino);

static inline size_t dbfs_dirent_next(guint16 namelen)
{
	return ALIGN(sizeof(struct dbfs_dirent) + namelen, DBFS_DIRENT_ALIGN);
}

static inline size_t dbfs_xlist_next(guint16 namelen)
{
	return ALIGN(sizeof(struct dbfs_xlist) + namelen, DBFS_XLIST_ALIGN);
}

#endif /* __DBFS_H__ */
