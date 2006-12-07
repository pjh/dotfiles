
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <attr/xattr.h>
#include <db.h>
#include "dbfs.h"

static int dbfs_xattr_list_read(DB_TXN *txn, DBT *key, DBT *val, char *keystr, guint64 ino)
{
	snprintf(keystr, 32, "/xattr/%Lu", (unsigned long long) ino);

	memset(key, 0, sizeof(*key));
	key->data = keystr;
	key->size = strlen(keystr);

	memset(val, 0, sizeof(*val));
	val->flags = DB_DBT_MALLOC;

	return gfs->meta->get(gfs->meta, txn, key, val, 0);
}

static int dbfs_xattr_list_add(DB_TXN *txn, guint64 ino, const char *name)
{
	struct dbfs_xlist *ent;
	char keystr[32];
	DBT key, val;
	size_t alloc_len;
	size_t name_len = strlen(name);
	int rc;

	/* get list from db */
	rc = dbfs_xattr_list_read(txn, &key, &val, keystr, ino);
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
	rc = gfs->meta->put(gfs->meta, txn, &key, &val, 0) ? -EIO : 0;

out:
	free(val.data);
	return rc;
}

static int dbfs_xattr_list_del(DB_TXN *txn, guint64 ino, const char *name)
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
	rc = dbfs_xattr_list_read(txn, &key, &val, keystr, ino);
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
		rc = gfs->meta->put(gfs->meta, txn, &key, &val, 0) ? -EIO : 0;
	}

	/* otherwise, delete db entry */
	else
		rc = dbmeta_del(txn, keystr);

out:
	free(val.data);
	return rc;
}

int dbfs_xattr_list(DB_TXN *txn, guint64 ino, void **buf_out, size_t *buflen_out)
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
	rc = dbfs_xattr_list_read(txn, &key, &val, keystr, ino);
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

static int dbfs_xattr_read(DB_TXN *txn, guint64 ino, const char *name, DBT *val)
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

	rc = gfs->meta->get(gfs->meta, txn, &key, val, 0);
	if (rc == DB_NOTFOUND)
		return -EINVAL;
	return rc ? -EIO : 0;
}

static int dbfs_xattr_write(DB_TXN *txn, guint64 ino, const char *name,
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

	return gfs->meta->put(gfs->meta, txn, &key, &val, 0) ? -EIO : 0;
}

int dbfs_xattr_get(DB_TXN *txn, guint64 ino_n, const char *name,
		   void **buf_out, size_t *buflen_out)
{
	int rc;
	DBT val;

	rc = dbfs_xattr_read(txn, ino_n, name, &val);
	if (rc)
		return rc;

	*buf_out = val.data;
	*buflen_out = val.size;

	return 0;
}

int dbfs_xattr_set(DB_TXN *txn, guint64 ino_n, const char *name,
		   const void *buf, size_t buflen, int flags)
{
	void *current = NULL;
	size_t current_len = 0;
	size_t name_len = strlen(name);
	int rc, exists;

	if ((!name) || (!*name) || (name_len > DBFS_XATTR_NAME_LEN) ||
	    (!g_utf8_validate(name, name_len, NULL)) ||
	    (buflen > DBFS_XATTR_MAX_LEN))
		return -EINVAL;

	rc = dbfs_xattr_get(txn, ino_n, name, &current, &current_len);
	if (rc && (rc != -EINVAL))
		return rc;

	exists = (current == NULL);
	free(current);

	if (exists && (flags & XATTR_CREATE))
		return -EEXIST;
	if (!exists && (flags & XATTR_REPLACE))
		return -ENOATTR;

	rc = dbfs_xattr_write(txn, ino_n, name, buf, buflen);
	if (rc)
		return rc;

	rc = dbfs_xattr_list_add(txn, ino_n, name);
	if (rc)
		return rc;

	return 0;
}

int dbfs_xattr_remove(DB_TXN *txn, guint64 ino_n, const char *name,
		      gboolean update_list)
{
	char key_str[DBFS_XATTR_NAME_LEN + 32];

	snprintf(key_str, sizeof(key_str),
		 "/xattr/%Lu/%s", (unsigned long long) ino_n, name);

	if (update_list) {
		int rc = dbfs_xattr_list_del(txn, ino_n, name);
		if (rc)
			return rc;
	}

	return dbmeta_del(txn, key_str);
}

