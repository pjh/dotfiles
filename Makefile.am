##
## Toplevel Makefile.am
##

INCLUDES	= @GNET_CFLAGS@ @FUSE_CFLAGS@

sbin_PROGRAMS	= ndbfs

ndbfs_SOURCES	= ndbfs.c
ndbfs_LDADD	= @GNET_LIBS@ @FUSE_LIBS@

EXTRA_DIST	= autogen.sh

