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

#endif /* __DBFS_H__ */
