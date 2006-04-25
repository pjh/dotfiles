
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <glib.h>
#include <db.h>
#include "dbfs.h"

struct dbfs_lookup_info {
	const char	*name;
	size_t		namelen;
	guint64		*ino;
};

struct dbfs_dirscan_info {
	const char		*name;
	size_t			namelen;
	void			*start_ent;
	void			*end_ent;
};

void dbfs_inode_free(struct dbfs_inode *ino)
{
	free(ino->raw_inode);
	g_free(ino);
}

static int dbmeta_del(const char *key_str)
{
	DBT key;
	int rc;

	key.data = (void *) key_str;
	key.size = strlen(key_str);

	/* delete key 'key_str' from metadata database */
	rc = gfs->meta->del(gfs->meta, NULL, &key, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return -EIO;
	return 0;
}

static int dbfs_inode_del(struct dbfs_inode *ino)
{
	guint64 ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	char key[32];
	int rc, rrc;

	sprintf(key, "/inode/%Lu", (unsigned long long) ino_n);

	rrc = dbmeta_del(key);

	switch (ino->type) {
	case IT_REG:
		/* FIXME */
		break;

	case IT_DIR:
		sprintf(key, "/dir/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(key);
		if (rc && !rrc)
			rrc = rc;
		break;

	case IT_SYMLINK:
		sprintf(key, "/symlink/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(key);
		if (rc && !rrc)
			rrc = rc;
		break;

	case IT_DEV:
	case IT_FIFO:
	case IT_SOCKET:
		/* nothing additional to delete */
		break;
	}

	return rrc;
}

static int dbfs_inode_write(struct dbfs_inode *ino)
{
	struct dbfs_raw_inode *raw_ino = ino->raw_inode;
	guint64 ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	DBT key, val;
	char key_str[32];

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	key.data = key_str;
	key.size = strlen(key_str);

	val.data = raw_ino;
	val.size = ino->raw_ino_size;

	raw_ino->version = GUINT64_TO_LE(
		GUINT64_FROM_LE(raw_ino->version) + 1);

	return gfs->meta->get(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;
}

static int dbfs_mode_type(guint32 mode, enum dbfs_inode_type *itype)
{
	if (S_ISDIR(mode))
		*itype = IT_DIR;
	else if (S_ISCHR(mode) || S_ISBLK(mode))
		*itype = IT_DEV;
	else if (S_ISFIFO(mode))
		*itype = IT_FIFO;
	else if (S_ISLNK(mode))
		*itype = IT_SYMLINK;
	else if (S_ISSOCK(mode))
		*itype = IT_SOCKET;
	else if (S_ISREG(mode))
		*itype = IT_REG;
	else
		return -EINVAL;

	return 0;
}

int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_inode *ino;
	size_t ex_sz;
	guint32 mode;

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	key.data = key_str;
	key.size = strlen(key_str);

	val.flags = DB_DBT_MALLOC;

	rc = gfs->meta->get(gfs->meta, NULL, &key, &val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return rc;

	/* calculate size of struct dbfs_extent area */
	ex_sz = val.size - sizeof(struct dbfs_raw_inode);

	/* initialize runtime information about the inode */
	ino = g_new(struct dbfs_inode, 1);
	ino->n_extents = ex_sz / sizeof(struct dbfs_extent);
	ino->raw_ino_size = val.size;
	ino->raw_inode = val.data;

	/* deduce inode type */
	mode = GUINT32_FROM_LE(ino->raw_inode->mode);
	rc = dbfs_mode_type(mode, &ino->type);
	g_assert(rc == 0);

	*ino_out = ino;

	return 0;
}

int dbfs_symlink_read(guint64 ino, DBT *val)
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

	rc = gfs->meta->get(gfs->meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

int dbfs_dir_read(guint64 ino, DBT *val)
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

	rc = gfs->meta->get(gfs->meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOTDIR;
	return rc ? -EIO : 0;
}

static int dbfs_dir_write(guint64 ino, DBT *val)
{
	DBT key;
	char key_str[32];

	memset(&key, 0, sizeof(key));

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	key.data = key_str;
	key.size = strlen(key_str);

	return gfs->meta->put(gfs->meta, NULL, &key, val, 0) ? -EIO : 0;
}

int dbfs_dir_foreach(void *dir, dbfs_dir_actor_t func, void *userdata)
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

		/* send dirent to callback function */
		rc = func(de, userdata);
		if (rc)
			break;

		/* align things so that compiler structures
		 * do not wind up misaligned.
		 */
		p += sizeof(struct dbfs_dirent) + de->namelen +
		     (4 - (de->namelen & 0x3));
	}

	return rc;
}

static int dbfs_dir_scan1(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	if (!di->start_ent) {
		if ((de->namelen == di->namelen) &&
		    (!memcmp(de->name, di->name, di->namelen)))
			di->start_ent = de;
	}
	else if (!di->end_ent) {
		di->end_ent = de;
		return 1;
	}

	return 0;
}

int dbfs_dir_lookup(guint64 parent, const char *name, guint64 *ino)
{
	struct dbfs_dirscan_info di;
	struct dbfs_dirent *de;
	DBT val;
	int rc;

	*ino = 0;

	/* read directory from database */
	rc = dbfs_dir_read(parent, &val);
	if (rc)
		return rc;

	memset(&di, 0, sizeof(di));
	di.name = name;
	di.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_scan1, &di);
	if (!rc || !di.start_ent) {
		rc = -ENOENT;
		goto out;
	}
	if (rc != 1)
		goto out;

	/* if match found, return inode number */
	de = di.start_ent;
	*ino = de->ino;

out:
	free(val.data);
	return rc;
}

static int dbfs_dirent_del(guint64 parent, const char *name)
{
	struct dbfs_dirscan_info ui;
	DBT dir_val;
	int rc, del_len, tail_len;

	rc = dbfs_dir_read(parent, &dir_val);
	if (rc)
		return rc;

	memset(&ui, 0, sizeof(ui));
	ui.name = name;
	ui.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(dir_val.data, dbfs_dir_scan1, &ui);
	if (rc != 1) {
		free(dir_val.data);
		return -ENOENT;
	}

	del_len = ui.end_ent - ui.start_ent;
	tail_len = (dir_val.data + dir_val.size) - ui.end_ent;

	memmove(ui.start_ent, ui.end_ent, tail_len);
	dir_val.size -= del_len;

	rc = dbfs_dir_write(parent, &dir_val);

	free(dir_val.data);

	return rc;
}

int dbfs_unlink(guint64 parent, const char *name, unsigned long flags)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc, is_dir;
	guint32 nlink;

	rc = dbfs_dir_lookup(parent, name, &ino_n);
	if (rc)
		goto out;

	if (ino_n == DBFS_ROOT_INO) {
		rc = -EINVAL;
		goto out;
	}

	rc = dbfs_inode_read(ino_n, &ino);
	if (rc)
		goto out;

	is_dir = S_ISDIR(GUINT32_FROM_LE(ino->raw_inode->mode));
	if (is_dir && (!(flags & DBFS_UNLINK_DIR))) {
		rc = -EISDIR;
		goto out_ino;
	}

	rc = dbfs_dirent_del(parent, name);
	if (rc)
		goto out_ino;

	nlink = GUINT32_FROM_LE(ino->raw_inode->nlink);
	if (is_dir && (nlink <= 2))
		nlink = 0;
	else
		nlink--;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	if (!nlink)
		rc = dbfs_inode_del(ino);
	else
		rc = dbfs_inode_write(ino);

out_ino:
	dbfs_inode_free(ino);
out:
	return rc;
}

int dbfs_mknod(guint64 parent, const char *name, guint32 mode, guint64 rdev,
	       struct dbfs_inode **ino)
{
	/* FIXME */
	*ino = NULL;
	return -EIO;

	/* To do:

	   - fail if invalid name
	   - allocate new inode number
	   - create inode record
	   - if dir, create dir record
	   - attempt to add name to parent directory
	   - fail if already exists

	 */
}

