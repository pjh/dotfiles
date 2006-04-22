##
## Toplevel Makefile.am
##

INCLUDES	= @GNET_CFLAGS@ @FUSE_CFLAGS@

sbin_PROGRAMS	= dbfs

dbfs_SOURCES	= dbfs.c
dbfs_LDADD	= @GNET_LIBS@ @FUSE_LIBS@

EXTRA_DIST	= autogen.sh

