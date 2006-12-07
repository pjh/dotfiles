
/*
 * Maintained by Jeff Garzik <jgarzik@pobox.com>
 *
 * Copyright 2006 Red Hat, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <syslog.h>
#include <glib.h>
#include <db.h>
#include <openssl/sha.h>
#include "dbfs.h"

struct dbfs_lookup_info {
	const char	*name;
	size_t		namelen;
	guint64		*ino;
};

struct dbfs_dirscan_info {
	const char		*name;
	size_t			namelen;
	void			*ent;
};

static int dbfs_data_unref(DB_TXN *txn, const dbfs_blk_id_t *id);

int dbmeta_del(DB_TXN *txn, const char *key_str)
{
	DBT key;
	int rc;

	key.data = (void *) key_str;
	key.size = strlen(key_str);

	/* delete key 'key_str' from metadata database */
	rc = gfs->meta->del(gfs->meta, txn, &key, 0);
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

static int dbfs_inode_del_data(DB_TXN *txn, const struct dbfs_inode *ino)
{
	int i, rc = 0;

	for (i = 0; i < ino->n_extents; i++) {
		rc = dbfs_data_unref(txn, &ino->raw_inode->blocks[i].id);
		if (rc)
			return rc;
	}

	return 0;
}

int dbfs_inode_del(DB_TXN *txn, const struct dbfs_inode *ino)
{
	guint64 ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	char key[32];
	int rc = 0;

	switch (ino->type) {
	case IT_REG:
		rc = dbfs_inode_del_data(txn, ino);
		break;

	case IT_DIR:
		sprintf(key, "/dir/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(txn, key);
		break;

	case IT_SYMLINK:
		sprintf(key, "/symlink/%Lu", (unsigned long long) ino_n);
		rc = dbmeta_del(txn, key);
		break;

	case IT_DEV:
	case IT_FIFO:
	case IT_SOCKET:
		/* nothing additional to delete */
		break;
	}

	if (rc)
		goto out;

	sprintf(key, "/inode/%Lu", (unsigned long long) ino_n);

	rc = dbmeta_del(txn, key);

out:
	return rc;
}

int dbfs_inode_read(DB_TXN *txn, guint64 ino_n, struct dbfs_inode **ino_out)
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

	rc = gfs->meta->get(gfs->meta, txn, &key, &val, 0);
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

static int dbfs_inode_next(DB_TXN *txn, struct dbfs_inode **ino_out)
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
		rc = dbfs_inode_read(txn, gfs->next_inode, &ino);
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

int dbfs_dir_read(DB_TXN *txn, guint64 ino, DBT *val)
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

	rc = gfs->meta->get(gfs->meta, txn, &key, val, 0);
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

	if ((GUINT16_FROM_LE(de->namelen) == di->namelen) &&
	    (!memcmp(de->name, di->name, di->namelen))) {
		di->ent = de;
		return 1;
	}

	return 0;
}

int dbfs_dir_lookup(DB_TXN *txn, guint64 parent, const char *name, guint64 *ino)
{
	struct dbfs_dirscan_info di;
	struct dbfs_dirent *de;
	DBT val;
	int rc;

	*ino = 0;

	/* read directory from database */
	rc = dbfs_dir_read(txn, parent, &val);
	if (rc)
		return rc;

	memset(&di, 0, sizeof(di));
	di.name = name;
	di.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_scan1, &di);
	if (rc != 1) {
		if (rc == 0)
			rc = -ENOENT;
		goto out;
	}
	rc = 0;

	/* if match found, return inode number */
	de = di.ent;
	*ino = de->ino;

out:
	free(val.data);
	return rc;
}

static int dbfs_dirent_del(DB_TXN *txn, guint64 parent, const char *name)
{
	struct dbfs_dirscan_info ui;
	DBT dir_val;
	int rc, del_len, tail_len;
	void *end_ent;
	struct dbfs_dirent *de;

	rc = dbfs_dir_read(txn, parent, &dir_val);
	if (rc)
		return rc;

	memset(&ui, 0, sizeof(ui));
	ui.name = name;
	ui.namelen = strlen(name);

	/* query pointer to start of matching dirent */
	rc = dbfs_dir_foreach(dir_val.data, dbfs_dir_scan1, &ui);
	if (rc != 1) {
		free(dir_val.data);
		if (rc == 0)
			rc = -ENOENT;
		return rc;
	}

	de = ui.ent;
	end_ent = ui.ent + dbfs_dirent_next(GUINT16_FROM_LE(de->namelen));

	del_len = end_ent - ui.ent;
	tail_len = (dir_val.data + dir_val.size) - end_ent;

	memmove(ui.ent, end_ent, tail_len);
	dir_val.size -= del_len;

	rc = dbfs_dir_write(txn, parent, &dir_val);

	free(dir_val.data);

	return rc;
}

static int dbfs_dir_find_last(struct dbfs_dirent *de, void *userdata)
{
	struct dbfs_dirscan_info *di = userdata;

	di->ent = de;

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
	if (strlen(name) > DBFS_FILENAME_MAX)
		return -EINVAL;
	return 0;
}

static int dbfs_dir_append(DB_TXN *txn, guint64 parent, guint64 ino_n,
			   const char *name)
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
	rc = dbfs_dir_read(txn, parent, &val);
	if (rc)
		return rc;

	memset(&di, 0, sizeof(di));
	di.name = name;
	di.namelen = strlen(name);

	/* scan for name in directory, abort if found */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_scan1, &di);
	if (rc != 0) {
		if (rc > 0)
			rc = -EEXIST;
		goto out;
	}

	/* get pointer to last entry */
	rc = dbfs_dir_foreach(val.data, dbfs_dir_find_last, &di);
	if (rc)
		goto out;

	/* adjust pointer 'p' to point to terminator entry */
	de = p = di.ent;
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

	rc = dbfs_dir_write(txn, parent, &val);

out:
	free(val.data);
	return rc;
}

int dbfs_symlink_read(DB_TXN *txn, guint64 ino, DBT *val)
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

	rc = gfs->meta->get(gfs->meta, txn, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

int dbfs_symlink_write(DB_TXN *txn, guint64 ino, const char *link)
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

	return gfs->meta->put(gfs->meta, txn, &key, &val, 0) ? -EIO : 0;
}

int dbfs_link(DB_TXN *txn, struct dbfs_inode *ino, guint64 ino_n,
	      guint64 parent, const char *name)
{
	guint32 nlink;
	int rc;

	/* make sure it doesn't exist yet */
	rc = dbfs_dir_append(txn, parent, ino_n, name);
	if (rc)
		return rc;

	/* increment link count */
	nlink = GUINT32_FROM_LE(ino->raw_inode->nlink);
	nlink++;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	/* write inode; if fails, DB TXN undoes directory modification */
	rc = dbfs_inode_write(txn, ino);

	return rc;
}

int dbfs_unlink(DB_TXN *txn, guint64 parent, const char *name,
		unsigned long flags)
{
	struct dbfs_inode *ino;
	guint64 ino_n;
	int rc, is_dir;
	guint32 nlink;

	rc = dbfs_dir_lookup(txn, parent, name, &ino_n);
	if (rc)
		goto out;

	if (ino_n == DBFS_ROOT_INO) {
		rc = -EINVAL;
		goto out;
	}

	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		goto out;

	is_dir = S_ISDIR(GUINT32_FROM_LE(ino->raw_inode->mode));
	if (is_dir && (!(flags & DBFS_UNLINK_DIR))) {
		rc = -EISDIR;
		goto out_ino;
	}

	rc = dbfs_dirent_del(txn, parent, name);
	if (rc)
		goto out_ino;

	nlink = GUINT32_FROM_LE(ino->raw_inode->nlink);
	if (is_dir && (nlink <= 2))
		nlink = 0;
	else
		nlink--;
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);

	if (!nlink)
		rc = dbfs_inode_del(txn, ino);
	else
		rc = dbfs_inode_write(txn, ino);

out_ino:
	dbfs_inode_free(ino);
out:
	return rc;
}

int dbfs_mknod(DB_TXN *txn, guint64 parent, const char *name, guint32 mode,
	       guint64 rdev, struct dbfs_inode **ino_out)
{
	struct dbfs_inode *ino;
	int rc, is_dir;
	unsigned int nlink = 1;
	guint64 ino_n;

	*ino_out = NULL;

	rc = dbfs_inode_next(txn, &ino);
	if (rc)
		return rc;

	is_dir = S_ISDIR(mode);
	if (is_dir)
		nlink++;
	ino_n = GUINT64_FROM_LE(ino->raw_inode->ino);
	ino->raw_inode->mode = GUINT32_TO_LE(mode);
	ino->raw_inode->nlink = GUINT32_TO_LE(nlink);
	ino->raw_inode->rdev = GUINT64_TO_LE(rdev);

	rc = dbfs_inode_write(txn, ino);
	if (rc)
		goto err_out;

	if (is_dir) {
		rc = dbfs_dir_new(txn, parent, ino_n, ino);
		if (rc)
			goto err_out_del;
	}

	rc = dbfs_dir_append(txn, parent, ino_n, name);
	if (rc)
		goto err_out_del;

	*ino_out = ino;
	return 0;

err_out_del:
	/* txn abort will delete inode */
err_out:
	dbfs_inode_free(ino);
	return rc;
}

int dbfs_rename(DB_TXN *txn, guint64 parent, const char *name,
		guint64 new_parent, const char *new_name)
{
	int rc;
	guint64 ino_n;

	if ((parent == new_parent) && (!strcmp(name, new_name)))
		return -EINVAL;

	rc = dbfs_dir_lookup(txn, parent, name, &ino_n);
	if (rc)
		return rc;

	rc = dbfs_dirent_del(txn, parent, name);
	if (rc)
		return rc;

	rc = dbfs_unlink(txn, new_parent, new_name, 0);
	if (rc && (rc != -ENOENT))
		return rc;

	return dbfs_dir_append(txn, new_parent, ino_n, new_name);
}

static void ext_list_free(GList *ext_list)
{
	GList *tmp;

	tmp = ext_list;
	while (tmp) {
		g_free(tmp->data);
		tmp = tmp->next;
	}
	g_list_free(ext_list);
}

static int dbfs_ext_match(const struct dbfs_inode *ino, guint64 off,
			  guint32 rd_size, GList **ext_list_out)
{
	struct dbfs_extent *ext;
	guint64 pos;
	guint32 ext_len, ext_off, ext_list_want, bytes = 0;
	GList *ext_list = *ext_list_out;
	gboolean in_frag;
	unsigned int i;
	int rc;

	pos = 0;
	in_frag = FALSE;
	ext_list_want = rd_size;
	for (i = 0; i < ino->n_extents; i++) {
		ext_len = GUINT32_FROM_LE(ino->raw_inode->blocks[i].len);
		ext_off = GUINT32_FROM_LE(ino->raw_inode->blocks[i].off);

		if ((!in_frag) && ((pos + ext_len) > off)) {
			guint32 skip;

			ext = g_new(struct dbfs_extent, 1);
			if (!ext) {
				rc = -ENOMEM;
				goto err_out;
			}

			memcpy(&ext->id, &ino->raw_inode->blocks[i].id,
			       sizeof(dbfs_blk_id_t));
			skip = off - pos;
			ext->off = skip + ext_off;
			ext->len = MIN(ext_len - skip, ext_list_want);

			ext_list = g_list_append(ext_list, ext);
			ext_list_want -= ext->len;
			bytes += ext->len;
			in_frag = TRUE;
		}

		else if (in_frag) {
			ext = g_new(struct dbfs_extent, 1);
			if (!ext) {
				rc = -ENOMEM;
				goto err_out;
			}

			memcpy(&ext->id, &ino->raw_inode->blocks[i].id,
			       sizeof(dbfs_blk_id_t));
			ext->off = ext_off;
			ext->len = MIN(ext_len, ext_list_want);

			ext_list = g_list_append(ext_list, ext);
			ext_list_want -= ext->len;
			bytes += ext->len;

			if (ext_list_want == 0)
				break;
		}

		pos += ext_len;
	}

	*ext_list_out = ext_list;
	return (int) bytes;

err_out:
	ext_list_free(ext_list);
	*ext_list_out = NULL;
	return rc;
}

static const char zero_block[DBFS_ZERO_CMP_BLK_SZ];

static gboolean is_zero_buf(const void *buf, size_t buflen)
{
	while (buflen > 0) {
		size_t cmp_size;
		int rc;

		cmp_size = MIN(buflen, DBFS_ZERO_CMP_BLK_SZ);
		rc = memcmp(buf, zero_block, cmp_size);
		if (rc)
			return FALSE;

		buflen -= cmp_size;
	}

	return TRUE;
}

static gboolean is_null_id(const dbfs_blk_id_t *id)
{
	return is_zero_buf(id, DBFS_BLK_ID_LEN);
}

static int dbfs_ext_read(DB_TXN *txn, const dbfs_blk_id_t *id, void *buf,
			 unsigned int offset, size_t *buflen)
{
	DBT key, val;
	int rc;

	memset(&key, 0, sizeof(key));
	key.data = (void *) id;
	key.size = DBFS_BLK_ID_LEN;

	memset(&val, 0, sizeof(val));
	val.data = buf;
	val.ulen = *buflen;
	val.dlen = *buflen;
	val.doff = offset;
	val.flags = DB_DBT_USERMEM | DB_DBT_PARTIAL;

	rc = gfs->data->get(gfs->data, txn, &key, &val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return rc;

	*buflen = val.size;
	return 0;
}

int dbfs_read(DB_TXN *txn, guint64 ino_n, guint64 off, size_t read_req_size,
	      void **buf_out)
{
	struct dbfs_inode *ino;
	GList *tmp, *ext_list = NULL;
	void *buf = NULL;
	size_t buflen = 0;
	unsigned int pos = 0;
	int rc;

	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		goto out;

	rc = dbfs_ext_match(ino, off, read_req_size, &ext_list);
	if (rc <= 0)
		goto out_ino;
	buflen = rc;

	buf = malloc(buflen);
	if (!buf) {
		rc = -ENOMEM;
		goto out_list;
	}

	tmp = ext_list;
	while (tmp) {
		struct dbfs_extent *ext;

		ext = tmp->data;
		if (is_null_id(&ext->id)) {
			memset(buf + pos, 0, ext->len);
		} else {
			size_t fraglen = ext->len;

			rc = dbfs_ext_read(txn, &ext->id, buf + pos,
					   ext->off, &fraglen);
			if (rc) {
				free(buf);
				buf = NULL;
				goto out_list;
			}
			if (fraglen != ext->len) {
				free(buf);
				buf = NULL;
				rc = -EINVAL;
				goto out_list;
			}
		}

		pos += ext->len;

		tmp = tmp->next;
	}

out_list:
	ext_list_free(ext_list);
out_ino:
	dbfs_inode_free(ino);
out:
	*buf_out = buf;
	return rc < 0 ? rc : buflen;
}

static int dbfs_write_unique_buf(DB_TXN *txn, const DBT *key, const void *buf,
				 size_t buflen)
{
	struct dbfs_hashref ref;
	DBT val;
	int rc;

	ref.refs = GUINT32_TO_LE(1);

	memset(&val, 0, sizeof(val));
	val.data = &ref;
	val.size = sizeof(ref);

	rc = gfs->hashref->put(gfs->hashref, txn, (DBT *) key, &val, 0);
	if (rc)
		return -EIO;

	memset(&val, 0, sizeof(val));
	val.data = (void *) buf;
	val.size = buflen;

	rc = gfs->data->put(gfs->data, txn, (DBT *) key, &val, DB_NOOVERWRITE);
	if (rc)
		return -EIO;

	return 0;
}

static int dbfs_write_buf(DB_TXN *txn, const void *buf, size_t buflen,
			  struct dbfs_extent *ext)
{
	struct dbfs_hashref *ref;
	DBT key, val;
	int rc;

	if (buflen == 0 || buflen > DBFS_MAX_EXT_LEN)
		return -EINVAL;

	memset(ext, 0, sizeof(*ext));
	ext->len = GUINT32_TO_LE(buflen);

	if (is_zero_buf(buf, buflen))
		return 0;

	SHA1(buf, buflen, (unsigned char *) &ext->id);

	memset(&key, 0, sizeof(key));
	key.data = &ext->id;
	key.size = DBFS_BLK_ID_LEN;

	memset(&val, 0, sizeof(val));
	val.flags = DB_DBT_MALLOC;

	rc = gfs->hashref->get(gfs->hashref, txn, &key, &val, 0);
	if (rc && (rc != DB_NOTFOUND))
		return -EIO;

	if (rc == DB_NOTFOUND)
		return dbfs_write_unique_buf(txn, &key, buf, buflen);

	ref = val.data;
	g_assert(val.size == sizeof(*ref));

	ref->refs = GUINT32_TO_LE(GUINT32_FROM_LE(ref->refs) + 1);

	rc = gfs->hashref->put(gfs->hashref, txn, &key, &val, 0);
	free(val.data);

	return rc ? -EIO : 0;
}

static int dbfs_data_unref(DB_TXN *txn, const dbfs_blk_id_t *id)
{
	struct dbfs_hashref *ref;
	guint32 refs;
	DBT key, val;
	int rc, rc2;

	if (is_null_id(id))
		return 0;

	memset(&key, 0, sizeof(key));
	key.data = (void *) id;
	key.size = DBFS_BLK_ID_LEN;

	memset(&val, 0, sizeof(val));
	val.flags = DB_DBT_MALLOC;

	rc = gfs->hashref->get(gfs->hashref, txn, &key, &val, 0);
	if (rc == DB_NOTFOUND)
		return -ENOENT;
	if (rc)
		return -EIO;

	ref = val.data;
	g_assert(val.size == sizeof(*ref));
	refs = GUINT32_FROM_LE(ref->refs);

	if (refs > 1) {
		refs--;
		ref->refs = GUINT32_TO_LE(refs);

		rc = gfs->hashref->put(gfs->hashref, txn, &key, &val, 0);
		free(val.data);

		return rc ? -EIO : 0;
	}

	free(val.data);

	rc = gfs->hashref->del(gfs->hashref, txn, &key, 0);
	rc2 = gfs->data->del(gfs->data, txn, &key, 0);

	return (rc || rc2) ? -EIO : 0;
}

static int dbfs_inode_realloc(struct dbfs_inode *ino,
			      unsigned int new_n_extents)
{
	struct dbfs_raw_inode *new_raw;
	size_t new_size = sizeof(struct dbfs_raw_inode) +
		(sizeof(struct dbfs_extent) * new_n_extents);

	new_raw = g_malloc0(new_size);
	if (!new_raw)
		return -ENOMEM;

	memcpy(new_raw, ino->raw_inode, ino->raw_ino_size);

	free(ino->raw_inode);
	ino->raw_inode = new_raw;
	ino->raw_ino_size = new_size;
	ino->n_extents = new_n_extents;

	return 0;
}

int dbfs_inode_resize(DB_TXN *txn, struct dbfs_inode *ino, guint64 new_size)
{
	guint64 old_size, diff, diff_ext;
	unsigned int grow, i, new_n_extents, tmp;
	int rc;

	old_size = GUINT64_FROM_LE(ino->raw_inode->size);
	grow = (old_size < new_size);
	diff = grow ? new_size - old_size : old_size - new_size;

	diff_ext = (diff / DBFS_MAX_EXT_LEN);
	if (diff % DBFS_MAX_EXT_LEN)
		diff_ext++;

	if (grow) {
		unsigned int old_n_extents = ino->n_extents;
		new_n_extents = ino->n_extents + diff_ext;

		rc = dbfs_inode_realloc(ino, new_n_extents);
		if (rc)
			return rc;

		for (i = old_n_extents; i < new_n_extents; i++) {
			g_assert(diff > 0);
			tmp = MIN(diff, DBFS_MAX_EXT_LEN);

			memset(&ino->raw_inode->blocks[i], 0,
				sizeof(struct dbfs_extent));
			ino->raw_inode->blocks[i].len =
				GUINT32_TO_LE(tmp);
			diff -= tmp;
		}

		g_assert(diff == 0);
	}

	else {
		struct dbfs_extent *ext;
		guint32 ext_len;

		new_n_extents = ino->n_extents;
		while (new_n_extents > 0) {
			ext = &ino->raw_inode->blocks[new_n_extents - 1];
			ext_len = GUINT32_FROM_LE(ext->len);
			if (ext_len > diff)
				break;

			rc = dbfs_data_unref(txn, &ext->id);
			if (rc)
				return rc;

			memset(ext, 0, sizeof(*ext));
			ino->n_extents--;

			new_n_extents--;
			diff -= ext_len;
		}

		if (diff > 0) {
			ext = &ino->raw_inode->blocks[new_n_extents - 1];
			ext_len = GUINT32_FROM_LE(ext->len);
			ext_len -= diff;
			ext->len = GUINT32_TO_LE(ext_len);

			g_assert(ext_len > 0);
		}

		rc = dbfs_inode_realloc(ino, new_n_extents);
		if (rc)
			return rc;
	}

	ino->raw_inode->size = GUINT64_TO_LE(new_size);
	return 0;
}

int dbfs_write(DB_TXN *txn, guint64 ino_n, guint64 off, const void *buf,
	       size_t buflen)
{
	struct dbfs_extent ext;
	struct dbfs_inode *ino;
	int rc;
	guint64 i_size, old_i_size;

	rc = dbfs_inode_read(txn, ino_n, &ino);
	if (rc)
		return rc;

	old_i_size = i_size = GUINT64_FROM_LE(ino->raw_inode->size);

	if (debugging)
		syslog(LOG_DEBUG, "dbfs_write: ino %Lu, off %Lu, len %zu, i_size %Lu",
		       (unsigned long long) ino_n,
		       (unsigned long long) off,
		       buflen,
		       (unsigned long long) i_size);

	if ((0xffffffffffffffffULL - off) < buflen)
		return -EINVAL;

	rc = dbfs_write_buf(txn, buf, buflen, &ext);
	if (rc)
		goto out;

	if (off > i_size) {
		rc = dbfs_inode_resize(txn, ino, off + 1);
		if (rc)
			goto err_out;
	}

	/* read again; it may have changed after calling dbfs_inode_resize() */
	i_size = GUINT64_FROM_LE(ino->raw_inode->size);

	if (debugging && (i_size != old_i_size))
		syslog(LOG_DEBUG, "dbfs_write: i_size updated to %Lu",
		       (unsigned long long) i_size);

	/* append */
	if (off == i_size) {
		unsigned int idx;

		rc = dbfs_inode_resize(txn, ino, i_size + buflen);
		if (rc)
			goto err_out;

		idx = ino->n_extents - 1;

		g_assert(is_null_id(&ino->raw_inode->blocks[idx].id));
		memcpy(&ino->raw_inode->blocks[idx], &ext, sizeof(ext));

		goto out_write;
	}

	/* FIXME: update data in middle of file */
	syslog(LOG_ERR, "dbfs_write: should be updating data in middle of file!");

out_write:
	rc = dbfs_inode_write(txn, ino);
	if (rc == 0)
		rc = buflen;
	goto out;

err_out:
out:
	dbfs_inode_free(ino);
	return rc;
}
