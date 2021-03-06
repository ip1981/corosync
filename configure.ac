AC_PREREQ([2.61])

AC_INIT([corosync], [1.99.8], [discuss@lists.corosync.org])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

dnl Need _POSIX_THREAD_SEMANTICS on Illumos/Solaris
dnl for getgrnam_r or readdir_r
AC_USE_SYSTEM_EXTENSIONS

AM_INIT_AUTOMAKE([foreign])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES])

AC_PROG_CC
AM_PROG_CC_C_O
LT_INIT

AC_CONFIG_SRCDIR([lib/cpg.c])
AC_CONFIG_HEADER([include/corosync/config.h])

with_sun_ld=no
if $LD --help 2>&1 | $GREP 'M mapfile' > /dev/null; then
    with_sun_ld=yes
fi
AM_CONDITIONAL([SUNLD], [test $with_sun_ld = yes])
AM_CONDITIONAL([GNULD], [test $with_gnu_ld = yes])

sinclude(coroysync-default.m4)

AC_CHECK_PROGS([GROFF], [groff])
AC_CHECK_PROGS([AUGTOOL], [augtool])
AC_CHECK_PROGS([DOT], [dot])
AC_CHECK_PROGS([DOXYGEN], [doxygen])

# Checks for libraries.
AC_SEARCH_LIBS([dlopen], [dl])
AC_SEARCH_LIBS([pthread_create], [pthread])
AC_SEARCH_LIBS([socket], [socket])
AC_SEARCH_LIBS([sched_getscheduler], [rt])

PKG_CHECK_MODULES([nss], [nss])
PKG_CHECK_MODULES([LIBQB], [libqb])

dnl What if libqb is in a strange place?
save_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS $LIBQB_LIBS"
AC_CHECK_LIB([qb], [qb_log_thread_priority_set], \
	     have_qb_log_thread_priority_set="yes", \
	     have_qb_log_thread_priority_set="no")
if test "x${have_qb_log_thread_priority_set}" = xyes; then
	AC_DEFINE_UNQUOTED([HAVE_QB_LOG_THREAD_PRIORITY_SET], 1, [have qb_log_thread_priority_set])
fi
LDFLAGS="$save_LDFLAGS"

# Checks for header files.
AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([ \
arpa/inet.h \
asm/types.h \
fcntl.h \
getopt.h \
ifaddrs.h \
limits.h \
linux/rtnetlink.h \
malloc.h \
net/if.h \
net/if_var.h \
netdb.h \
netinet/in.h \
netinet/in_var.h \
sys/ioctl.h \
sys/param.h \
sys/socket.h \
sys/sockio.h \
sys/time.h \
syslog.h \
utmpx.h \
], [], [],
[
#ifdef HAVE_NET_IF_H
# include <net/if.h>
#endif
])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T
AC_C_VOLATILE

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_REPLACE_FNMATCH
AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_TYPE_SIGNAL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([alarm alphasort atexit bzero dup2 endgrent endpwent fcntl \
		getcwd getpeerucred getpeereid gettimeofday inet_ntoa memmove \
		memset mkdir scandir select socket strcasecmp strchr strdup \
		strerror strrchr strspn strstr pthread_setschedparam \
		sched_get_priority_max sched_setscheduler getifaddrs])

AC_CONFIG_FILES([Makefile
    exec/Makefile
    include/Makefile
    lib/Makefile
    common_lib/Makefile
    man/Makefile
    pkgconfig/Makefile
    test/Makefile
    cts/Makefile
    cts/agents/Makefile
    cts/CTSvars.py
    pkgconfig/corosync.pc
    pkgconfig/libcfg.pc
    pkgconfig/libcmap.pc
    pkgconfig/libcorosync_common.pc
    pkgconfig/libcpg.pc
    pkgconfig/libquorum.pc
    pkgconfig/libsam.pc
    pkgconfig/libtotem_pg.pc
    pkgconfig/libvotequorum.pc
    tools/Makefile
    conf/Makefile
    Doxyfile])


# local options

AC_ARG_ENABLE([debug],
	[AC_HELP_STRING([--enable-debug], [enable debug build])],
	[default="no"])

AC_ARG_ENABLE([small-memory-footprint],
	[AC_HELP_STRING([--enable-small-memory-footprint], [Use small message queues and small messages sizes])],
	[default="no"])

AC_ARG_ENABLE([dbus],
	[AC_HELP_STRING([--enable-dbus], [dbus events])],,
	[enable_dbus="no"])

AC_ARG_ENABLE([snmp],
    [AC_HELP_STRING([--enable-snmp], [SNMP protocol support])],
    [default="no"])

AC_ARG_ENABLE([testagents],
	[AC_HELP_STRING([--enable-testagents], [Install Test Agents])],,
	[default="no"])
AM_CONDITIONAL(ENABLE_TESTAGENTS, test x$enable_testagents = xyes)

AC_ARG_ENABLE([rdma],
	[AC_HELP_STRING([--enable-rdma], [Infiniband RDMA transport support])],,
	[enable_rdma="no"])
AM_CONDITIONAL(ENABLE_RDMA, test x$enable_rdma = xyes)

AC_ARG_ENABLE([monitoring],
	[AC_HELP_STRING([--enable-monitoring], [resource monitoring])],,
	[default="no"])
AM_CONDITIONAL(BUILD_MONITORING, test x$enable_monitoring = xyes)

AC_ARG_ENABLE([watchdog],
	[AC_HELP_STRING([--enable-watchdog], [Watchdog support])],,
	[default="no"])
AM_CONDITIONAL(ENABLE_WATCHDOG, test x$enable_watchdog = xyes)

AC_ARG_ENABLE([augeas],
	[AC_HELP_STRING([--enable-augeas], [Install the augeas lens for corosync.conf])],,
	[enable_augeas="no"])
AM_CONDITIONAL(ENABLE_AUGEAS, test x$enable_augeas = xyes)

AC_ARG_ENABLE([xmlconf],
       [AC_HELP_STRING([--enable-xmlconf], [XML configuration support])],,
       [enable_xmlconf="no" ])
AM_CONDITIONAL(ENABLE_XMLCONF, test x$enable_xmlconf = xyes)

AC_ARG_WITH([libstatgrab],
            AC_HELP_STRING([--with-libstatgrab], [use libstatgrab for monitoring. \
                            Default is "no" on Linux and "yes" on other systems]),
            [with_libstatgrab=$withval],
            [with_libstatgrab=no]
            )

if test "x${enable_monitoring}" = xyes; then
    PACKAGE_FEATURES="$PACKAGE_FEATURES monitoring"
    case "$host_os:$with_libstatgrab" in
        *linux*:no) ;;
        *)
            PKG_CHECK_MODULES([LIBSTATGRAB], [libstatgrab >= 0.17])
            AC_DEFINE([WITH_LIBSTATGRAB], [1], [Define to 1 if using libstatgrab])
    esac
fi

# debug build stuff
if test "x${enable_debug}" = xyes; then
	AC_DEFINE_UNQUOTED([DEBUG], [1], [Compiling Debugging code])
	PACKAGE_FEATURES="$PACKAGE_FEATURES debug"
fi

# Look for dbus-1
if test "x${enable_dbus}" = xyes; then
	PKG_CHECK_MODULES([DBUS],[dbus-1])
	AC_DEFINE_UNQUOTED([HAVE_DBUS], 1, [have dbus])
	PACKAGE_FEATURES="$PACKAGE_FEATURES dbus"
fi
AM_CONDITIONAL([ENABLE_DBUS], [test $enable_dbus = yes])

if test "x${enable_testagents}" = xyes; then
	AC_DEFINE_UNQUOTED([HAVE_TESTAGENTS], 1, [have testagents])
	PACKAGE_FEATURES="$PACKAGE_FEATURES testagents"
fi

if test "x${enable_rdma}" = xyes; then
	PKG_CHECK_MODULES([rdmacm],[rdmacm])
	PKG_CHECK_MODULES([ibverbs],[ibverbs])
	AC_DEFINE_UNQUOTED([HAVE_RDMA], 1, [have rdmacm])
	PACKAGE_FEATURES="$PACKAGE_FEATURES rdma"
fi

if test "x${enable_watchdog}" = xyes; then
	AC_CHECK_HEADER(linux/watchdog.h,,AC_MSG_ERROR(watchdog requires linux/watchdog.h))
	AC_CHECK_HEADER(linux/reboot.h,,AC_MSG_ERROR(watchdog requires linux/reboot.h))
	AC_DEFINE_UNQUOTED([HAVE_WATCHDOG], 1, [have watchdog])
	PACKAGE_FEATURES="$PACKAGE_FEATURES watchdog"
fi

if test "x${enable_augeas}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES augeas"
fi
if test "x${enable_systemd}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES systemd"
fi
if test "x${enable_xmlconf}" = xyes; then
	PACKAGE_FEATURES="$PACKAGE_FEATURES xmlconf"
fi

if test "x${enable_snmp}" = xyes; then
       SNMPCONFIG=""
       AC_CHECK_HEADERS(net-snmp/net-snmp-config.h)

       if test "x${ac_cv_header_net_snmp_net_snmp_config_h}" != "xyes"; then
               enable_snmp=no
       fi

       if test $enable_snmp != no; then
               AC_PATH_PROGS(SNMPCONFIG, net-snmp-config)
               if test "X${SNMPCONFIG}" = "X"; then
                       AC_MSG_RESULT(You need the net_snmp development package to continue.)
                       enable_snmp=no
               fi
       fi

       if test $enable_snmp != no; then
               AC_MSG_CHECKING(for special snmp libraries)
               SNMPLIBS=`$SNMPCONFIG --libs`
               AC_MSG_RESULT($SNMPLIBS)
       fi

       if test $enable_snmp != no; then
               savedLibs=$LIBS
               LIBS="$LIBS $SNMPLIBS"
               AC_CHECK_FUNCS(netsnmp_transport_open_client)
               if test $ac_cv_func_netsnmp_transport_open_client != yes; then
                       AC_CHECK_FUNCS(netsnmp_tdomain_transport)
                       if test $ac_cv_func_netsnmp_tdomain_transport != yes; then
                               enable_snmp=no
                       fi
               else
                       AC_DEFINE_UNQUOTED([NETSNMPV54], $NETSNMP_NEW_SUPPORT, [have net-snmp5.4 over])
               fi
               LIBS=$savedLibs
       fi

       AC_MSG_CHECKING(for snmp)
       AC_MSG_RESULT($enable_snmp)
   if test $enable_snmp = no; then
               enable_snmp=0
               AC_MSG_ERROR(Unable to support SNMP)
   else
               enable_snmp=1
               PACKAGE_FEATURES="$PACKAGE_FEATURES snmp"
               AC_DEFINE_UNQUOTED([ENABLE_SNMP], $enable_snmp, [Build in support for sending SNMP traps])
   fi
else
       enable_snmp=0
fi
AC_SUBST([SNMPLIBS])
AM_CONDITIONAL([ENABLE_SNMP], [test $enable_snmp = 1])



if test "x${enable_small_memory_footprint}" = xyes ; then
	AC_DEFINE_UNQUOTED([HAVE_SMALL_MEMORY_FOOTPRINT], 1, [have small_memory_footprint])
	PACKAGE_FEATURES="$PACKAGE_FEATURES small-memory-footprint"
fi

# substitute what we need:
AC_SUBST([INITDDIR])

AM_CONDITIONAL(AUGTOOL, test -n "${AUGTOOL}")
AC_SUBST([NSS_LDFLAGS])

AM_CONDITIONAL(BUILD_HTML_DOCS, test -n "${GROFF}")

AC_SUBST([LINT_FLAGS])

AC_DEFINE_UNQUOTED([LOCALSTATEDIR], "$(eval echo ${localstatedir})", [localstate directory])

COROSYSCONFDIR=${sysconfdir}/corosync
AC_SUBST([COROSYSCONFDIR])
AC_DEFINE_UNQUOTED([COROSYSCONFDIR], "$(eval echo ${COROSYSCONFDIR})", [corosync config directory])

AC_DEFINE_UNQUOTED([PACKAGE_FEATURES], "${PACKAGE_FEATURES}", [corosync built-in features])

AC_OUTPUT

