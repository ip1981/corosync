noinst_PROGRAMS = \
cpgbench \
cpgbenchzc \
cpgbound \
cpgverify \
stress_cpgcontext \
stress_cpgfdget \
stress_cpgzc \
testcpg \
testcpg2 \
testcpgzc \
testquorum \
testsam \
testvotequorum1 \
testvotequorum2	\
testzcgc

noinst_SCRIPTS = ploadstart
CLEANFILES = $(noinst_SCRIPTS)
EXTRA_DIST = ploadstart.sh
ploadstart: ploadstart.sh Makefile
	cat $< > $@
	chmod 755 $@


AM_CPPFLAGS = \
	-I$(top_srcdir)/include

AM_LDFLAGS  = \
	-L$(top_builddir)/common_lib \
	-L$(top_builddir)/lib \
	-L$(top_builddir)/exec

cpgbench_LDADD            = -lcpg $(LIBQB_LIBS)

cpgbenchzc_LDADD          = -lcpg -lcorosync_common $(LIBQB_LIBS)

cpgbound_LDADD            = -lcpg $(LIBQB_LIBS)

cpgverify_CFLAGS          = $(nss_CFLAGS)
cpgverify_LDADD           = -lcpg -ltotem_pg $(LIBQB_LIBS) $(nss_LIBS)

stress_cpgcontext_LDADD   = -lcpg $(LIBQB_LIBS)

stress_cpgfdget_LDADD     = -lcpg $(LIBQB_LIBS)

stress_cpgzc_LDADD        = -lcpg $(LIBQB_LIBS)

testcpg2_LDADD            = -lcpg $(LIBQB_LIBS)

testcpg_LDADD             = -lcpg $(LIBQB_LIBS)

testcpgzc_LDADD           = -lcpg $(LIBQB_LIBS)

testquorum_LDADD          = -lquorum $(LIBQB_LIBS)

testsam_LDADD             = -lsam -lcmap -lquorum $(LIBQB_LIBS)

testvotequorum1_LDADD     = -lvotequorum $(LIBQB_LIBS)

testvotequorum2_LDADD     = -lvotequorum $(LIBQB_LIBS)

testzcgc_LDADD            = -lcpg $(LIBQB_LIBS)


