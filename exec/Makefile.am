lib_LTLIBRARIES = libtotem_pg.la
sbin_PROGRAMS = corosync

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include

libtotem_pg_la_CFLAGS = $(nss_CFLAGS)
libtotem_pg_la_LIBADD = $(nss_LIBS) $(LIBQB_LIBS)
libtotem_pg_la_LDFLAGS = -version-info 5:0:0

libtotem_pg_la_SOURCES = \
totemip.c \
totemnet.c \
totemudp.c \
totemudpu.c \
totemrrp.c \
totemsrp.c \
totemmrp.c \
totempg.c \
cs_queue.h \
totemcrypto.c

if ENABLE_RDMA
libtotem_pg_la_SOURCES += totemiba.c
endif


corosync_CFLAGS  = $(nss_CFLAGS) $(LIBSTATGRAB_CFLAGS)
corosync_LDFLAGS = -L$(top_builddir)/common_lib
corosync_LDADD   = libtotem_pg.la -lcorosync_common $(LIBSTATGRAB_LIBS)
corosync_SOURCES = \
apidef.c \
cfg.c \
cmap.c \
coroparse.c \
cpg.c \
icmap.c \
ipc_glue.c \
logsys.c \
main.c \
mainconfig.c \
mon.c \
pload.c \
quorum.c \
schedwrk.c \
service.c \
sync.c \
timer.c \
totemconfig.c \
util.c \
votequorum.c \
vsf_quorum.c \
vsf_ykd.c

if ENABLE_WATCHDOG
corosync_SOURCES += wd.c
endif

noinst_HEADERS		= apidef.h mainconfig.h main.h \
			  quorum.h service.h timer.h totemconfig.h \
			  totemmrp.h totemnet.h totemudp.h totemiba.h totemrrp.h \
			  totemudpu.h totemsrp.h util.h vsf.h schedwrk.h \
			  sync.h fsm.h votequorum.h vsf_ykd.h totemcrypto.h

