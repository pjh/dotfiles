##
## Toplevel Makefile.am
##

INCLUDES	= @GNET_CFLAGS@ @FUSE_CFLAGS@

sbin_PROGRAMS	= ndbfs dbfs

ndbfs_SOURCES	= ndbfs.c
ndbfs_LDADD	= @GNET_LIBS@ @FUSE_LIBS@

dbfs_SOURCES	= dbfs.c dbfs.h dbfs-backend.c
dbfs_LDADD	= @GNET_LIBS@ @FUSE_LIBS@ @DB_LIBS@

EXTRA_DIST	= autogen.sh

