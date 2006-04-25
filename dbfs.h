#ifndef __DBFS_H__
#define __DBFS_H__

#include <glib.h>

enum {
	DBFS_BLK_ID_LEN		= 20,

	DBFS_UNLINK_DIR		= (1 << 0),

	DBFS_ROOT_INO		= 1,
};

enum {
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

struct dbfs_extent {
	dbfs_blk_id_t	id;
	guint64		size;
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
	const char		*passwd;

	DB_ENV			*env;
	DB			*meta;

	guint64			next_inode;
};


typedef int (*dbfs_dir_actor_t) (struct dbfs_dirent *, void *);

extern int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out);
extern int dbfs_dir_read(guint64 ino, DBT *val);
extern int dbfs_symlink_read(guint64 ino, DBT *val);
extern int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata);
extern int dbfs_dir_lookup(guint64 parent, const char *name, guint64 *ino);
extern int dbfs_unlink(guint64 parent, const char *name, unsigned long flags);
extern void dbfs_inode_free(struct dbfs_inode *ino);
extern void dbfs_init(void *userdata);
extern void dbfs_exit(void *userdata);
extern int dbfs_mknod(guint64 parent, const char *name,
		      guint32 mode, guint64 rdev,
		      struct dbfs_inode **ino);
extern int dbfs_symlink_write(guint64 ino, const char *link);
extern int dbfs_inode_del(struct dbfs_inode *ino);

extern int dbfs_open(struct dbfs *fs);
extern void dbfs_close(struct dbfs *fs);
extern struct dbfs *dbfs_new(void);
extern void dbfs_free(struct dbfs *fs);
extern struct dbfs *gfs;

#endif /* __DBFS_H__ */
