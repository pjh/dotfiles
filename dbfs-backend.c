
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
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

void dbfs_inode_free(struct dbfs_inode *ino)
{
	free(ino->raw_inode);
	g_free(ino);
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

static int dbfs_inode_next(struct dbfs_inode **ino_out)
{
	struct dbfs_inode *ino;
	int rc;
	guint64 curtime, start = gfs->next_inode;

	*ino_out = NULL;

	/* loop through inode numbers starting at gfs->next_inode,
	 * and stop on first error.  Error ENOENT means the
	 * inode number was not found, and can therefore be used
	 * as the next free inode number.
	 */
	while (1) {
		rc = dbfs_inode_read(gfs->next_inode, &ino);
		if (rc)
			break;

		dbfs_inode_free(ino);
		gfs->next_inode++;
		if (gfs->next_inode < 2)
			gfs->next_inode = 2;
		if (gfs->next_inode == start) {	/* loop */
			rc = -EBUSY;
			break;
		}
	}

	if (rc != -ENOENT)
		return rc;

	/* allocate an empty inode */
	ino = g_new0(struct dbfs_inode, 1);
	ino->raw_ino_size = sizeof(struct dbfs_raw_inode);
	ino->raw_inode = malloc(ino->raw_ino_size);
	memset(ino->raw_inode, 0, ino->raw_ino_size);

	ino->raw_inode->ino = GUINT64_TO_LE(gfs->next_inode++);
	curtime = GUINT64_TO_LE(time(NULL));
	ino->raw_inode->ctime = curtime;
	ino->raw_inode->atime = curtime;
	ino->raw_inode->mtime = curtime;

	*ino_out = ino;

	return 0;
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
	unsigned int namelen;

	p = dir;
	while (1) {
		de = p;

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
		namelen = GUINT16_FROM_LE(de->namelen);
		p += sizeof(struct dbfs_dirent) + namelen +
		     (4 - (namelen & 0x3));
	}

	return rc;
}

static int dbfs_dir_scan1(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	if (!di->start_ent) {
		if ((GUINT16_FROM_LE(de->namelen) == di->namelen) &&
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

static int dbfs_dir_new(guint64 parent, guint64 ino_n, struct dbfs_inode *ino)
{
	void *mem, *p, *q;
	struct dbfs_dirent *de;
	size_t namelen;
	DBT val;
	int rc;

	p = mem = malloc(128);
	memset(mem, 0, 128);

	/*
	 * add entry for "."
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);
	de->namelen = GUINT16_TO_LE(1);
	de->ino = GUINT64_TO_LE(ino_n);

	q = p + sizeof(struct dbfs_dirent);
	memcpy(q, ".", 1);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	/*
	 * add entry for ".."
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);
	de->namelen = GUINT16_TO_LE(2);
	de->ino = GUINT64_TO_LE(parent);

	q = p + sizeof(struct dbfs_dirent);
	memcpy(q, "..", 2);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	/*
	 * add terminating entry
	 */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);

	namelen = GUINT16_FROM_LE(de->namelen);
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	/*
	 * store dir in database
	 */
	memset(&val, 0, sizeof(val));
	val.data = mem;
	val.size = p - mem;

	rc = dbfs_dir_write(ino_n, &val);
	if (rc) {
		dbfs_inode_del(ino);
		dbfs_inode_free(ino);
		return rc;
	}

	free(mem);

	return 0;
}

static int dbfs_dir_find_last(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	di->end_ent = de;

	return 0;
}

static int dbfs_dir_append(guint64 parent, guint64 ino_n, const char *name)
{
	struct dbfs_dirscan_info di;
	struct dbfs_dirent *de;
	DBT val;
	int rc;
	unsigned int dir_size, namelen;
	void *p;

	/* read parent directory from database */
	rc = dbfs_dir_read(parent, &val);
	if (rc)
		return rc;

	memset(&di, 0, sizeof(di));
	di.name = name;
	di.namelen = strlen(name);

	/* scan for name in directory, abort if found */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_scan1, &di);
	if (rc != -ENOENT) {
		if (rc == 0)
			rc = -EEXIST;
		goto out;
	}

	/* get pointer to last entry */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_find_last, &di);
	if (rc)
		goto out;

	/* adjust pointer 'p' to point to terminator entry */
	de = p = di.end_ent;
	namelen = GUINT16_FROM_LE(de->namelen);
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	/* increase directory data area size */
	dir_size = p - val.data;
	val.size = dir_size + (3 * sizeof(struct dbfs_dirent)) + di.namelen;
	val.data = realloc(val.data, val.size);
	p = val.data + dir_size;

	/* append directory entry */
	de = p;
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);
	de->namelen = GUINT16_TO_LE(di.namelen);
	de->ino = GUINT64_TO_LE(ino_n);
	memcpy(de->name, name, di.namelen);

	namelen = di.namelen;
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	/* append terminator entry */
	de = p;
	memset(de, 0, sizeof(*de));
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);

	namelen = 0;
	p += sizeof(struct dbfs_dirent) + namelen +
	     (4 - (namelen & 0x3));

	val.size = p - val.data;

	rc = dbfs_dir_write(parent, &val);

out:
	free(val.data);
	return rc;
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

static int dbfs_name_validate(const char *name)
{
	if (strchr(name, '/'))
		return -EINVAL;
	if (!g_utf8_validate(name, -1, NULL))
		return -EINVAL;
	return 0;
}

int dbfs_mknod(guint64 parent, const char *name, guint32 mode, guint64 rdev,
	       struct dbfs_inode **ino_out)
{
	struct dbfs_inode *ino;
	int rc, is_dir;
	unsigned int nlink = 1;
	guint64 ino_n;

	*ino_out = NULL;

	rc = dbfs_name_validate(name);
	if (rc)
		return rc;

	rc = dbfs_inode_next(&ino);
	if (rc)
		return rc;

	is_dir = S_ISDIR(mode);
	if (is_dir)
		nlink++;
	ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	ino->raw_inode->mode = GUINT32_TO_LE(mode);
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);
	ino->raw_inode->rdev = GUINT64_TO_LE(rdev);

	rc = dbfs_inode_write(ino);
	if (rc)
		goto err_out;

	if (is_dir) {
		rc = dbfs_dir_new(parent, ino_n, ino);
		if (rc)
			goto err_out_del;
	}

	rc = dbfs_dir_append(parent, ino_n, name);
	if (rc)
		goto err_out_del;

	*ino_out = ino;
	return 0;

err_out_del:
	dbfs_inode_del(ino);
err_out:
	dbfs_inode_free(ino);
	return rc;
}

