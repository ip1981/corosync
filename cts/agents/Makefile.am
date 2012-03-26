bin_PROGRAMS = \
cpg_test_agent \
sam_test_agent \
votequorum_test_agent

agentdir = $(datadir)/$(PACKAGE)/tests
dist_agent_SCRIPTS = \
cmap-dispatch-deadlock.sh \
mem_leak_test.sh \
net_breaker.sh \
shm_leak_audit.sh

noinst_HEADERS = \
common_test_agent.h

AM_CPPFLAGS = \
	-I$(top_srcdir)/include

AM_LDFLAGS = \
	-L$(top_builddir)/lib \
	-L$(top_builddir)/common_lib

cpg_test_agent_SOURCES        = cpg_test_agent.c common_test_agent.c
cpg_test_agent_CFLAGS         = $(nss_CFLAGS)
cpg_test_agent_LDADD          = -lcpg -lcfg -lcorosync_common $(LIBQB_LIBS) $(nss_LIBS)

sam_test_agent_SOURCES        = sam_test_agent.c common_test_agent.c
sam_test_agent_LDADD          = -lsam -lquorum -lcmap -lcorosync_common $(LIBQB_LIBS)

votequorum_test_agent_SOURCES = votequorum_test_agent.c common_test_agent.c
votequorum_test_agent_LDADD   = -lvotequorum -lquorum -lcorosync_common $(LIBQB_LIBS)

