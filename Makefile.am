##
## Toplevel Makefile.am
##

INCLUDES	= @GLIB_CFLAGS@ @FUSE_CFLAGS@

noinst_LIBRARIES = libdbfs.a

sbin_PROGRAMS	= dbfs mkdbfs

libdbfs_a_SOURCES = libdbfs.c

dbfs_SOURCES	= dbfs.c dbfs.h dbfs-backend.c
dbfs_LDADD	= @GLIB_LIBS@ @FUSE_LIBS@ @DB_LIBS@ libdbfs.a

mkdbfs_SOURCES	= mkdbfs.c
mkdbfs_LDADD	= @GLIB_LIBS@ @DB_LIBS@ libdbfs.a

EXTRA_DIST	= autogen.sh

