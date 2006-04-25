##
## Toplevel Makefile.am
##

INCLUDES	= @GLIB_CFLAGS@ @FUSE_CFLAGS@

sbin_PROGRAMS	= dbfs mkdbfs

dbfs_SOURCES	= dbfs.c dbfs.h dbfs-backend.c libdbfs.c
dbfs_LDADD	= @GLIB_LIBS@ @FUSE_LIBS@ @DB_LIBS@

mkdbfs_SOURCES	= mkdbfs.c
mkdbfs_LDADD	= @DB_LIBS@

EXTRA_DIST	= autogen.sh

