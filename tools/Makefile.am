sbin_PROGRAMS = \
corosync-cfgtool \
corosync-cmapctl \
corosync-cpgtool \
corosync-fplay \
corosync-keygen \
corosync-notifyd \
corosync-quorumtool

AM_LDFLAGS = -L$(top_builddir)/common_lib -L$(top_builddir)/lib

AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include


# Automake will guess sources automatically

dist_bin_SCRIPTS = corosync-blackbox
if INSTALL_XMLCONF
bin_SCRIPTS = corosync-xmlproc
corosync-xmlproc: corosync-xmlproc.sh
	$(SED) 's#@''DATADIR@#${datadir}#g' $< > $@
endif
EXTRA_DIST = \
corosync-xmlproc.sh \
corosync-notifyd.sysconfig.example

corosync_cmapctl_LDFLAGS    = $(AM_LDFLAGS)
corosync_cmapctl_LDADD      = -lcmap -lcorosync_common $(LIBQB_LIBS)

corosync_fplay_LDADD        = $(LIBQB_LIBS)

corosync_cfgtool_LDFLAGS    = $(AM_LDFLAGS)
corosync_cfgtool_LDADD      = -lcfg #$(LIBQB_LIBS)

corosync_cpgtool_LDFLAGS    = $(AM_LDFLAGS)
corosync_cpgtool_LDADD      = -lcpg -lcfg $(LIBQB_LIBS)

corosync_quorumtool_LDFLAGS = $(AM_LDFLAGS)
corosync_quorumtool_LDADD   = -lcmap -lcfg -lquorum -lcorosync_common -lvotequorum $(LIBQB_LIBS)

corosync_notifyd_CFLAGS     =
corosync_notifyd_LDFLAGS    = $(AM_LDFLAGS)
corosync_notifyd_LDADD      = $(LIBQB_LIBS) -lcfg -lcmap -lquorum

if ENABLE_DBUS
corosync_notifyd_LDADD  += $(DBUS_LIBS) #$(SNMPLIBS)
corosync_notifyd_CFLAGS += $(DBUS_CFLAGS)
endif
if ENABLE_SNMP
corosync_notifyd_LDADD  += $(SNMPLIBS)
endif

