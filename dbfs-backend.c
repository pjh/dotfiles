
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <attr/xattr.h>
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

int dbfs_inode_del(struct dbfs_inode *ino)
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

int dbfs_inode_read(guint64 ino_n, struct dbfs_inode **ino_out)
{
	int rc;
	DBT key, val;
	char key_str[32];
	struct dbfs_inode *ino;
	size_t ex_sz;
	guint32 mode;

	sprintf(key_str, "/inode/%Lu", (unsigned long long) ino_n);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(&val, 0, sizeof(val));
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

	sprintf(key_str, "/dir/%Lu", (unsigned long long) ino);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(val, 0, sizeof(*val));
	val->flags = DB_DBT_MALLOC;

	rc = gfs->meta->get(gfs->meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOTDIR;
	return rc ? -EIO : 0;
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
		p += dbfs_dirent_next(namelen);
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

static int dbfs_dir_find_last(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	di->end_ent = de;

	return 0;
}

static int dbfs_name_validate(const char *name)
{
	if (strchr(name, '/'))
		return -EINVAL;
	if (!g_utf8_validate(name, -1, NULL))
		return -EINVAL;
	if (!strcmp(name, "."))
		return -EINVAL;
	if (!strcmp(name, ".."))
		return -EINVAL;
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

	rc = dbfs_name_validate(name);
	if (rc)
		return rc;

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
	p += dbfs_dirent_next(namelen);

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
	p += dbfs_dirent_next(namelen);

	/* append terminator entry */
	de = p;
	memset(de, 0, sizeof(*de));
	de->magic = GUINT32_TO_LE(DBFS_DE_MAGIC);

	namelen = 0;
	p += dbfs_dirent_next(namelen);

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

	sprintf(key_str, "/symlink/%Lu", (unsigned long long) ino);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(val, 0, sizeof(*val));
	val->flags = DB_DBT_MALLOC;

	rc = gfs->meta->get(gfs->meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

int dbfs_symlink_write(guint64 ino, const char *link)
{
	DBT key, val;
	char key_str[32];

	sprintf(key_str, "/symlink/%Lu", (unsigned long long) ino);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(&val, 0, sizeof(val));
	val.data = (void *) link;
	val.size = strlen(link);

	return gfs->meta->put(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;
}

int dbfs_link(struct dbfs_inode *ino, guint64 ino_n, guint64 parent,
	      const char *name)
{
	guint32 nlink;
	int rc;

	/* make sure it doesn't exist yet */
	rc = dbfs_dir_append(parent, ino_n, name);
	if (rc)
		return rc;

	/* increment link count */
	nlink = GUINT32_FROM_LE(ino->raw_inode->nlink);
	nlink++;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	/* write inode; if fails, undo directory modification */
	rc = dbfs_inode_write(ino);
	if (rc)
		dbfs_dirent_del(parent, name);

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
	       struct dbfs_inode **ino_out)
{
	struct dbfs_inode *ino;
	int rc, is_dir;
	unsigned int nlink = 1;
	guint64 ino_n;

	*ino_out = NULL;

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

static int dbfs_xattr_list_read(DBT *key, DBT *val, char *keystr, guint64 ino)
{
	snprintf(keystr, 32, "/xattr/%Lu", (unsigned long long) ino);

	memset(key, 0, sizeof(*key));
	key->data = keystr;
	key->size = strlen(keystr);

	memset(val, 0, sizeof(*val));
	val->flags = DB_DBT_MALLOC;

	return gfs->meta->get(gfs->meta, NULL, key, val, 0);
}

static int dbfs_xattr_list_add(guint64 ino, const char *name)
{
	struct dbfs_xlist *ent;
	char keystr[32];
	DBT key, val;
	size_t alloc_len;
	size_t name_len = strlen(name);
	int rc;

	/* get list from db */
	rc = dbfs_xattr_list_read(&key, &val, keystr, ino);
	if (rc && (rc != DB_NOTFOUND))
		return -EIO;

	alloc_len = dbfs_xlist_next(name_len);

	/* if list not found, create new one */
	if (rc == DB_NOTFOUND) {
		ent = malloc(alloc_len);
		if (!ent)
			return -ENOMEM;

		val.data = ent;
		val.size = alloc_len;
	}

	/* otherwise, append to existing list */
	else {
		void *mem;
		size_t old_size;

		alloc_len += val.size;
		mem = realloc(val.data, alloc_len);
		if (!mem) {
			rc = -ENOMEM;
			goto out;
		}

		old_size = val.size;
		val.data = mem;
		val.size = alloc_len;

		mem += old_size;
		ent = mem;
	}

	/* fill in list entry at tail of list */
	ent->namelen = GUINT32_TO_LE(name_len);
	memcpy(ent->name, name, name_len);

	/* store new list in db */
	rc = gfs->meta->put(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;

out:
	free(val.data);
	return rc;
}

static int dbfs_xattr_list_del(guint64 ino, const char *name)
{
	size_t name_len = strlen(name);
	struct dbfs_xlist *ent;
	char keystr[32];
	DBT key, val;
	int rc;
	long bytes;
	void *mem;
	size_t ssize = 0;
	unsigned int entries = 0;

	/* get list from db */
	rc = dbfs_xattr_list_read(&key, &val, keystr, ino);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return -EIO;

	/* find entry in list */
	mem = val.data;
	bytes = val.size;
	while (bytes > 0) {
		entries++;
		ent = mem;
		ssize = dbfs_xlist_next(GUINT32_FROM_LE(ent->namelen));
		if (ssize > bytes) {		/* data corrupt */
			rc = -EIO;
			goto out;
		}

		if (!memcmp(ent->name, name, name_len))
			break;

		bytes -= ssize;
	}

	/* if not found, exit */
	if (bytes <= 0) {
		rc = -ENOENT;
		goto out;
	}

	/* if at least one entry will exist post-delete, update db */
	if (entries > 1) {
		/* swallow entry */
		memmove(mem, mem + ssize, bytes - ssize);
		val.size -= ssize;

		/* store new list in db */
		rc = gfs->meta->put(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;
	}

	/* otherwise, delete db entry */
	else
		rc = dbmeta_del(keystr);

out:
	free(val.data);
	return rc;
}

int dbfs_xattr_list(guint64 ino, void **buf_out, size_t *buflen_out)
{
	struct dbfs_xlist *ent;
	char keystr[32];
	DBT key, val;
	void *mem, *name_list;
	size_t name_list_len, ssize, name_len;
	long bytes;
	char null = 0;
	int rc;

	*buf_out = NULL;
	*buflen_out = 0;

	/* get list from db */
	rc = dbfs_xattr_list_read(&key, &val, keystr, ino);
	if (rc == DB_NOTFOUND)
		return 0;
	if (rc)
		return -EIO;
	if (val.size == 0)
		return 0;

	/* allocate output buffer */
	name_list = malloc(val.size);
	if (!name_list) {
		rc = -ENOMEM;
		goto out;
	}
	name_list_len = 0;

	/* fill output buffer */
	mem = val.data;
	bytes = val.size;
	while (bytes > 0) {
		ent = mem;
		name_len = GUINT32_FROM_LE(ent->namelen);
		ssize = dbfs_xlist_next(name_len);

		if (ssize > bytes) {		/* data corrupt */
			rc = -EIO;
			goto out;
		}

		memcpy(name_list + name_list_len, ent->name, name_len);
		name_list_len += name_len;

		memcpy(name_list + name_list_len, &null, 1);
		name_list_len++;

		bytes -= ssize;
	}

	*buf_out = name_list;
	*buflen_out = name_list_len;

out:
	free(val.data);
	return rc;
}

static int dbfs_xattr_read(guint64 ino, const char *name, DBT *val)
{
	char key_str[DBFS_XATTR_NAME_LEN + 32];
	DBT key;
	int rc;

	snprintf(key_str, sizeof(key_str),
		 "/xattr/%Lu/%s", (unsigned long long) ino, name);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(val, 0, sizeof(*val));
	val->flags = DB_DBT_MALLOC;

	rc = gfs->meta->get(gfs->meta, NULL, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

static int dbfs_xattr_write(guint64 ino, const char *name,
			    const void *buf, size_t buflen)
{
	char key_str[DBFS_XATTR_NAME_LEN + 32];
	DBT key, val;

	snprintf(key_str, sizeof(key_str),
		 "/xattr/%Lu/%s", (unsigned long long) ino, name);

	memset(&key, 0, sizeof(key));
	key.data = key_str;
	key.size = strlen(key_str);

	memset(&val, 0, sizeof(val));
	val.data = (void *) buf;
	val.size = buflen;

	return gfs->meta->put(gfs->meta, NULL, &key, &val, 0) ? -EIO : 0;
}

int dbfs_xattr_get(guint64 ino_n, const char *name,
		   void **buf_out, size_t *buflen_out)
{
	int rc;
	DBT val;

	rc = dbfs_xattr_read(ino_n, name, &val);
	if (rc)
		return rc;
	
	*buf_out = val.data;
	*buflen_out = val.size;

	return 0;
}

int dbfs_xattr_set(guint64 ino_n, const char *name, const void *buf,
		   size_t buflen, int flags)
{
	void *current = NULL;
	size_t current_len = 0;
	size_t name_len = strlen(name);
	int rc, exists;

	if ((!name) || (!*name) || (name_len > DBFS_XATTR_NAME_LEN) ||
	    (!g_utf8_validate(name, name_len, NULL)) ||
	    (buflen > DBFS_XATTR_MAX_LEN))
		return -EINVAL;

	rc = dbfs_xattr_get(ino_n, name, &current, &current_len);
	if (rc && (rc != -EINVAL))
		return rc;

	exists = (current == NULL);
	free(current);

	if (exists && (flags & XATTR_CREATE))
		return -EEXIST;
	if (!exists && (flags & XATTR_REPLACE))
		return -ENOATTR;

	rc = dbfs_xattr_write(ino_n, name, buf, buflen);
	if (rc)
		return rc;

	rc = dbfs_xattr_list_add(ino_n, name);
	if (rc) {
		dbfs_xattr_remove(ino_n, name, FALSE);
		return rc;
	}

	return 0;
}

int dbfs_xattr_remove(guint64 ino_n, const char *name, gboolean update_list)
{
	char key_str[DBFS_XATTR_NAME_LEN + 32];

	snprintf(key_str, sizeof(key_str),
		 "/xattr/%Lu/%s", (unsigned long long) ino_n, name);

	if (update_list) {
		int rc = dbfs_xattr_list_del(ino_n, name);
		if (rc)
			return rc;
	}

	return dbmeta_del(key_str);
}

