EXTRA_DIST = \
COROSYNC-MIB.txt \
corosync-signals.conf \
corosync.conf.example \
corosync.conf.example.udpu \
corosync.xml.example \
lenses/corosync.aug \
lenses/tests/test_corosync.aug \
xml2conf.xsl

corosysconfdir   = ${COROSYSCONFDIR}
corosysconf_DATA = \
corosync.conf.example \
corosync.conf.example.udpu

if ENABLE_AUGEAS
corolensdir         = ${datadir}/augeas/lenses
corolens_DATA       = lenses/corosync.aug

corolenstestdir     = ${corolensdir}/tests
corolenstest_DATA   = lenses/tests/test_corosync.aug
endif

if ENABLE_XMLCONF
corosysxmlconfdir   = ${COROSYSCONFDIR}
corosysxmlconf_DATA = corosync.xml.example

corosysxmlxsltdir   = ${datadir}/corosync
corosysxmlxslt_DATA = xml2conf.xsl
endif

if ENABLE_SNMP
mibdir              = $(datadir)/snmp/mibs
mib_DATA            = COROSYNC-MIB.txt
endif

if ENABLE_DBUS
dbusdir             = $(sysconfdir)/dbus-1/system.d
dbus_DATA           = corosync-signals.conf
endif

