
pkgconfigdir = ${libdir}/pkgconfig

nodist_pkgconfig_DATA = \
corosync.pc \
libcfg.pc \
libcmap.pc \
libcorosync_common.pc \
libcpg.pc \
libquorum.pc \
libsam.pc \
libtotem_pg.pc \
libvotequorum.pc

EXTRA_DIST = $(nodist_pkgconfig_DATA:%.pc=%.pc.in)
CLEANFILES = $(nodist_pkgconfig_DATA)

