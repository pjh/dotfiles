#ifndef __DBFS_H__
#define __DBFS_H__

enum {
	DBFS_BLK_ID_LEN		= 20,
};

enum {
	DBFS_DE_MAGIC		= 0xd4d4d4d4U,
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
};

struct dbfs_extent {
	dbfs_blk_id_t	id;
	guint64		size;
};

struct dbfs_raw_inode {
	guint64		ino;
	guint64		generation;
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
};

struct dbfs_inode {
	unsigned int		n_extents;
	struct dbfs_raw_inode	raw_inode;
};


typedef int (*dbfs_dir_actor_t) (struct dbfs_dirent *, void *);

extern int dbfs_read_inode(guint64 ino_n, struct dbfs_inode **ino_out);
extern int dbfs_read_dir(guint64 ino, DBT *val);
extern int dbfs_read_link(guint64 ino, DBT *val);
extern int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata);
extern int dbfs_lookup(guint64 parent, const char *name, guint64 *ino);
extern int dbfs_unlink(guint64 parent, const char *name);

#endif /* __DBFS_H__ */
