lib_LTLIBRARIES = libcpg.la libquorum.la libcfg.la libvotequorum.la libsam.la libcmap.la

AM_CPPFLAGS = \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/include \
	$(LIBQB_CFLAGS)

AM_LDFLAGS = -L$(top_builddir)/common_lib

libcfg_la_LIBADD         = $(LIBQB_LIBS) -lcorosync_common
libcfg_la_SOURCES        = cfg.c util.h libcfg.versions
libcfg_la_LDFLAGS        = $(AM_LDFLAGS) -version-info 6:0:0

libcmap_la_LIBADD        = $(LIBQB_LIBS) -lcorosync_common
libcmap_la_SOURCES       = cmap.c util.h libcmap.versions
libcmap_la_LDFLAGS       = $(AM_LDFLAGS) -version-info 5:0:1

libcpg_la_LIBADD         = $(LIBQB_LIBS) -lcorosync_common
libcpg_la_SOURCES        = cpg.c util.h libcmap.versions
libcpg_la_LDFLAGS        = $(AM_LDFLAGS) -version-info 5:0:1

libquorum_la_LIBADD      = $(LIBQB_LIBS) -lcorosync_common
libquorum_la_SOURCES     = quorum.c util.h libquorum.versions
libquorum_la_LDFLAGS     = $(AM_LDFLAGS) -version-info 5:0:0

libvotequorum_la_LIBADD  = $(LIBQB_LIBS) -lcorosync_common
libvotequorum_la_SOURCES = votequorum.c util.h libvotequorum.versions
libvotequorum_la_LDFLAGS = $(AM_LDFLAGS) -version-info 5:0:0

libsam_la_LIBADD         = $(LIBQB_LIBS) -lcorosync_common -lcmap -lquorum
libsam_la_SOURCES        = sam.c util.h libsam.versions
libsam_la_DEPENDENCIES   = libcmap.la libquorum.la
libsam_la_LDFLAGS        = -L. $(AM_LDFLAGS) -version-info 8:0:4


if GNULD
libcfg_la_LDFLAGS        += -Wl,--version-script=$(srcdir)/libcfg.versions
libcmap_la_LDFLAGS       += -Wl,--version-script=$(srcdir)/libcmap.versions
libcpg_la_LDFLAGS        += -Wl,--version-script=$(srcdir)/libcpg.versions
libquorum_la_LDFLAGS     += -Wl,--version-script=$(srcdir)/libquorum.versions
libsam_la_LDFLAGS        += -Wl,--version-script=$(srcdir)/libsam.versions
libvotequorum_la_LDFLAGS += -Wl,--version-script=$(srcdir)/libvotequorum.versions
endif
if SUNLD
libcfg_la_LDFLAGS        += -Wl,-M -Wl,$(srcdir)/libcfg.versions
libcmap_la_LDFLAGS       += -Wl,-M -Wl,$(srcdir)/libcmap.versions
libcpg_la_LDFLAGS        += -Wl,-M -Wl,$(srcdir)/libcpg.versions
libquorum_la_LDFLAGS     += -Wl,-M -Wl,$(srcdir)/libquorum.versions
libsam_la_LDFLAGS        += -Wl,-M -Wl,$(srcdir)/libsam.versions
libvotequorum_la_LDFLAGS += -Wl,-M -Wl,$(srcdir)/libvotequorum.versions
endif

