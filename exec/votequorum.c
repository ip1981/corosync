/*
 * Copyright (c) 2009-2012 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Authors: Christine Caulfield (ccaulfie@redhat.com)
 *          Fabio M. Di Nitto   (fdinitto@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <sys/types.h>
#include <stdint.h>

#include <qb/qbipc_common.h>

#include "quorum.h"
#include <corosync/corodefs.h>
#include <corosync/list.h>
#include <corosync/logsys.h>
#include <corosync/coroapi.h>
#include <corosync/icmap.h>
#include <corosync/ipc_votequorum.h>

#include "service.h"

LOGSYS_DECLARE_SUBSYS ("VOTEQ");

/*
 * interface with corosync
 */

static struct corosync_api_v1 *corosync_api;

/*
 * votequorum global config vars
 */

#define DEFAULT_QDEVICE_TIMEOUT 10000

static char qdevice_name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
static struct cluster_node *qdevice = NULL;
static unsigned int qdevice_timeout = DEFAULT_QDEVICE_TIMEOUT;
static uint8_t qdevice_can_operate = 1;
static uint8_t qdevice_is_registered = 0;
static void *qdevice_reg_conn = NULL;

static uint8_t two_node = 0;

static uint8_t wait_for_all = 0;
static uint8_t wait_for_all_status = 0;

static uint8_t auto_tie_breaker = 0;
static int lowest_node_id = -1;

#define DEFAULT_LMS_WIN   10000
static uint8_t last_man_standing = 0;
static uint32_t last_man_standing_window = DEFAULT_LMS_WIN;

static uint8_t allow_downscale = 0;
static uint32_t ev_barrier = 0;

/*
 * votequorum_exec defines/structs/forward definitions
 */

struct req_exec_quorum_nodeinfo {
	struct   qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t nodeid;
	uint32_t votes;
	uint32_t expected_votes;
	uint32_t flags;
} __attribute__((packed));

struct req_exec_quorum_reconfigure {
	struct qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t nodeid;
	uint32_t value;
	uint8_t param;
	uint8_t _pad0;
	uint8_t _pad1;
	uint8_t _pad2;
} __attribute__((packed));

struct req_exec_quorum_qdevice_reg {
	struct		qb_ipc_request_header header __attribute__((aligned(8)));
	uint32_t	operation;
	char		qdevice_name[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
} __attribute__((packed));

struct req_exec_quorum_qdevice_reconfigure {
	struct	qb_ipc_request_header header __attribute__((aligned(8)));
	char	oldname[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
	char	newname[VOTEQUORUM_MAX_QDEVICE_NAME_LEN];
} __attribute__((packed));

/*
 * votequorum_exec onwire version (via totem)
 */

#include "votequorum.h"

/*
 * votequorum_exec onwire messages (via totem)
 */

#define MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO            0
#define MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE         1
#define MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_REG         2
#define MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_RECONFIGURE 3

static void votequorum_exec_send_expectedvotes_notification(void);
static int votequorum_exec_send_quorum_notification(void *conn, uint64_t context);

#define VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES 1
#define VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES     2

static int votequorum_exec_send_reconfigure(uint8_t param, unsigned int nodeid, uint32_t value);

/*
 * used by req_exec_quorum_qdevice_reg
 */
#define VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER 0
#define VOTEQUORUM_QDEVICE_OPERATION_REGISTER   1

/*
 * votequorum internal node status/view
 */

#define NODE_FLAGS_QUORATE        1
#define NODE_FLAGS_LEAVING        2
#define NODE_FLAGS_WFASTATUS      4
#define NODE_FLAGS_FIRST          8
#define NODE_FLAGS_QDEVICE       16
#define NODE_FLAGS_QDEVICE_STATE 32

#define NODEID_QDEVICE 0

typedef enum {
	NODESTATE_MEMBER=1,
	NODESTATE_DEAD,
	NODESTATE_LEAVING
} nodestate_t;

struct cluster_node {
	int         node_id;
	nodestate_t state;
	uint32_t    votes;
	uint32_t    expected_votes;
	uint32_t    flags;
	struct      list_head list;
};

/*
 * votequorum internal quorum status
 */

static uint8_t quorum;
static uint8_t cluster_is_quorate;

/*
 * votequorum membership data
 */

static struct cluster_node *us;
static struct list_head cluster_members_list;
static unsigned int quorum_members[PROCESSOR_COUNT_MAX];
static int quorum_members_entries = 0;
static struct memb_ring_id quorum_ringid;

/*
 * pre allocate all cluster_nodes + one for qdevice
 */
static struct cluster_node cluster_nodes[PROCESSOR_COUNT_MAX+2];
static int cluster_nodes_entries = 0;

/*
 * votequorum tracking
 */
struct quorum_pd {
	unsigned char track_flags;
	int tracking_enabled;
	uint64_t tracking_context;
	struct list_head list;
	void *conn;
};

static struct list_head trackers_list;

/*
 * votequorum timers
 */

static corosync_timer_handle_t qdevice_timer;
static int qdevice_timer_set = 0;
static corosync_timer_handle_t last_man_standing_timer;
static int last_man_standing_timer_set = 0;

/*
 * Service Interfaces required by service_message_handler struct
 */

static void votequorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id);

static quorum_set_quorate_fn_t quorum_callback;

/*
 * votequorum_exec handler and definitions
 */

static char *votequorum_exec_init_fn (struct corosync_api_v1 *api);
static int votequorum_exec_exit_fn (void);
static int votequorum_exec_send_nodeinfo(uint32_t nodeid);

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_nodeinfo_endian_convert (void *message);

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_reconfigure_endian_convert (void *message);

static void message_handler_req_exec_votequorum_qdevice_reg (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_qdevice_reg_endian_convert (void *message);

static void message_handler_req_exec_votequorum_qdevice_reconfigure (
	const void *message,
	unsigned int nodeid);
static void exec_votequorum_qdevice_reconfigure_endian_convert (void *message);

static struct corosync_exec_handler votequorum_exec_engine[] =
{
	{ /* 0 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_nodeinfo,
		.exec_endian_convert_fn	= exec_votequorum_nodeinfo_endian_convert
	},
	{ /* 1 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_reconfigure,
		.exec_endian_convert_fn	= exec_votequorum_reconfigure_endian_convert
	},
	{ /* 2 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_qdevice_reg,
		.exec_endian_convert_fn = exec_votequorum_qdevice_reg_endian_convert
	},
	{ /* 3 */
		.exec_handler_fn	= message_handler_req_exec_votequorum_qdevice_reconfigure,
		.exec_endian_convert_fn	= exec_votequorum_qdevice_reconfigure_endian_convert
	},
};

/*
 * Library Handler and Functions Definitions
 */

static int quorum_lib_init_fn (void *conn);

static int quorum_lib_exit_fn (void *conn);

static void message_handler_req_lib_votequorum_getinfo (void *conn,
							const void *message);

static void message_handler_req_lib_votequorum_setexpected (void *conn,
							    const void *message);

static void message_handler_req_lib_votequorum_setvotes (void *conn,
							 const void *message);

static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *message);

static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *message);

static void message_handler_req_lib_votequorum_qdevice_register (void *conn,
								 const void *message);

static void message_handler_req_lib_votequorum_qdevice_unregister (void *conn,
								   const void *message);

static void message_handler_req_lib_votequorum_qdevice_update (void *conn,
							       const void *message);

static void message_handler_req_lib_votequorum_qdevice_poll (void *conn,
							     const void *message);

static void message_handler_req_lib_votequorum_qdevice_getinfo (void *conn,
								const void *message);

static struct corosync_lib_handler quorum_lib_service[] =
{
	{ /* 0 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_getinfo,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 1 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_setexpected,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 2 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_setvotes,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 3 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_trackstart,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 4 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_trackstop,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 5 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_register,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 6 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_unregister,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 7 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_update,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 8 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_poll,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	},
	{ /* 9 */
		.lib_handler_fn		= message_handler_req_lib_votequorum_qdevice_getinfo,
		.flow_control		= COROSYNC_LIB_FLOW_CONTROL_NOT_REQUIRED
	}
};

static struct corosync_service_engine votequorum_service_engine = {
	.name				= "corosync vote quorum service v1.0",
	.id				= VOTEQUORUM_SERVICE,
	.priority			= 2,
	.private_data_size		= sizeof (struct quorum_pd),
	.allow_inquorate		= CS_LIB_ALLOW_INQUORATE,
	.flow_control			= COROSYNC_LIB_FLOW_CONTROL_REQUIRED,
	.lib_init_fn			= quorum_lib_init_fn,
	.lib_exit_fn			= quorum_lib_exit_fn,
	.lib_engine			= quorum_lib_service,
	.lib_engine_count		= sizeof (quorum_lib_service) / sizeof (struct corosync_lib_handler),
	.exec_init_fn			= votequorum_exec_init_fn,
	.exec_exit_fn			= votequorum_exec_exit_fn,
	.exec_engine			= votequorum_exec_engine,
	.exec_engine_count		= sizeof (votequorum_exec_engine) / sizeof (struct corosync_exec_handler),
	.confchg_fn			= votequorum_confchg_fn
};

struct corosync_service_engine *votequorum_get_service_engine_ver0 (void)
{
	return (&votequorum_service_engine);
}

static struct default_service votequorum_service[] = {
	{
		.name		= "corosync_votequorum",
		.ver		= 0,
		.loader		= votequorum_get_service_engine_ver0
	},
};

/*
 * common/utility macros/functions
 */

#define max(a,b) (((a) > (b)) ? (a) : (b))

#define list_iterate(v, head) \
	for (v = (head)->next; v != head; v = v->next)

static void node_add_ordered(struct cluster_node *newnode)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &newnode->list;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (newnode->node_id < node->node_id) {
			break;
		}
	}

	if (!node) {
		list_add(&newnode->list, &cluster_members_list);
	} else {
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}

	LEAVE();
}

static struct cluster_node *allocate_node(unsigned int nodeid)
{
	struct cluster_node *cl = NULL;
	struct list_head *tmp;

	ENTER();

	if (cluster_nodes_entries <= PROCESSOR_COUNT_MAX + 1) {
		cl = (struct cluster_node *)&cluster_nodes[cluster_nodes_entries];
		cluster_nodes_entries++;
	} else {
		list_iterate(tmp, &cluster_members_list) {
			cl = list_entry(tmp, struct cluster_node, list);
			if (cl->state == NODESTATE_DEAD) {
				break;
			}
		}
		/*
		 * this should never happen
		 */
		if (!cl) {
			log_printf(LOGSYS_LEVEL_CRIT, "Unable to find memory for node %u data!!", nodeid);
			goto out;
		}
		list_del(tmp);
	}

	memset(cl, 0, sizeof(struct cluster_node));
	cl->node_id = nodeid;
	if (nodeid != NODEID_QDEVICE) {
		node_add_ordered(cl);
	}

out:
	LEAVE();

	return cl;
}

static struct cluster_node *find_node_by_nodeid(unsigned int nodeid)
{
	struct cluster_node *node;
	struct list_head *tmp;

	ENTER();

	if (nodeid == us->node_id) {
		LEAVE();
		return us;
	}

	if (nodeid == NODEID_QDEVICE) {
		LEAVE();
		return qdevice;
	}

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if (node->node_id == nodeid) {
			LEAVE();
			return node;
		}
	}

	LEAVE();
	return NULL;
}

static void get_lowest_node_id(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;

	ENTER();

	lowest_node_id = us->node_id;

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id < lowest_node_id)) {
			lowest_node_id = node->node_id;
		}
	}
	log_printf(LOGSYS_LEVEL_DEBUG, "lowest node id: %d us: %d", lowest_node_id, us->node_id);
	icmap_set_uint32("runtime.votequorum.lowest_node_id", lowest_node_id);

	LEAVE();
}

static int check_low_node_id_partition(void)
{
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	int found = 0;

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		if ((node->state == NODESTATE_MEMBER) &&
		    (node->node_id == lowest_node_id)) {
				found = 1;
		}
	}

	LEAVE();
	return found;
}

static void decode_flags(uint32_t flags)
{
	log_printf(LOGSYS_LEVEL_DEBUG,
		   "flags: quorate: %s Leaving: %s WFA Status: %s First: %s Qdevice: %s QdeviceState: %s",
		   (flags & NODE_FLAGS_QUORATE)?"Yes":"No",
		   (flags & NODE_FLAGS_LEAVING)?"Yes":"No",
		   (flags & NODE_FLAGS_WFASTATUS)?"Yes":"No",
		   (flags & NODE_FLAGS_FIRST)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE)?"Yes":"No",
		   (flags & NODE_FLAGS_QDEVICE_STATE)?"Yes":"No");
}

static void update_wait_for_all_status(uint8_t wfa_status)
{
	wait_for_all_status = wfa_status;
	if (wait_for_all_status) {
		us->flags |= NODE_FLAGS_WFASTATUS;
	} else {
		us->flags &= ~NODE_FLAGS_WFASTATUS;
	}
	icmap_set_uint8("runtime.votequorum.wait_for_all_status",
			wait_for_all_status);
}

static void update_two_node(void)
{
	icmap_set_uint8("runtime.votequorum.two_node", two_node);
}

static void update_ev_barrier(uint32_t expected_votes)
{
	ev_barrier = expected_votes;
	icmap_set_uint32("runtime.votequorum.ev_barrier", ev_barrier);
}

static void update_qdevice_can_operate(uint8_t status)
{
	qdevice_can_operate = status;
	icmap_set_uint8("runtime.votequorum.qdevice_can_operate", qdevice_can_operate);
}

/*
 * quorum calculation core bits
 */

static int calculate_quorum(int allow_decrease, unsigned int max_expected, unsigned int *ret_total_votes)
{
	struct list_head *nodelist;
	struct cluster_node *node;
	unsigned int total_votes = 0;
	unsigned int highest_expected = 0;
	unsigned int newquorum, q1, q2;
	unsigned int total_nodes = 0;

	ENTER();

	if ((allow_downscale) && (allow_decrease) && (max_expected)) {
		max_expected = max(ev_barrier, max_expected);
	}

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);

		log_printf(LOGSYS_LEVEL_DEBUG, "node %u state=%d, votes=%u, expected=%u",
			   node->node_id, node->state, node->votes, node->expected_votes);

		if (node->state == NODESTATE_MEMBER) {
			if (max_expected) {
				node->expected_votes = max_expected;
			} else {
				highest_expected = max(highest_expected, node->expected_votes);
			}
			total_votes += node->votes;
			total_nodes++;
		}
	}

	if (qdevice_is_registered) {
		log_printf(LOGSYS_LEVEL_DEBUG, "node %u state=%d, votes=%u",
			   qdevice->node_id, qdevice->state, qdevice->votes);
		if (qdevice->state == NODESTATE_MEMBER) {
			total_votes += qdevice->votes;
			total_nodes++;
		}
	}

	if (max_expected > 0) {
		highest_expected = max_expected;
	}

	/*
	 * This quorum calculation is taken from the OpenVMS Cluster Systems
	 * manual, but, then, you guessed that didn't you
	 */
	q1 = (highest_expected + 2) / 2;
	q2 = (total_votes + 2) / 2;
	newquorum = max(q1, q2);

	/*
	 * Normally quorum never decreases but the system administrator can
	 * force it down by setting expected votes to a maximum value
	 */
	if (!allow_decrease) {
		newquorum = max(quorum, newquorum);
	}

	/*
	 * The special two_node mode allows each of the two nodes to retain
	 * quorum if the other fails.  Only one of the two should live past
	 * fencing (as both nodes try to fence each other in split-brain.)
	 * Also: if there are more than two nodes, force us inquorate to avoid
	 * any damage or confusion.
	 */
	if (two_node && total_nodes <= 2) {
		newquorum = 1;
	}

	if (ret_total_votes) {
		*ret_total_votes = total_votes;
	}

	LEAVE();
	return newquorum;
}

static void are_we_quorate(unsigned int total_votes)
{
	int quorate;
	int quorum_change = 0;

	ENTER();

	/*
	 * wait for all nodes to show up before granting quorum
	 */

	if ((wait_for_all) && (wait_for_all_status)) {
		if (total_votes != us->expected_votes) {
			log_printf(LOGSYS_LEVEL_NOTICE,
				   "Waiting for all cluster members. "
				   "Current votes: %d expected_votes: %d",
				   total_votes, us->expected_votes);
			cluster_is_quorate = 0;
			return;
		}
		update_wait_for_all_status(0);
	}

	if (quorum > total_votes) {
		quorate = 0;
	} else {
		quorate = 1;
		get_lowest_node_id();
	}

	if ((auto_tie_breaker) &&
	    (total_votes == (us->expected_votes / 2)) &&
	    (check_low_node_id_partition() == 1)) {
		quorate = 1;
	}

	if (cluster_is_quorate && !quorate) {
		quorum_change = 1;
		log_printf(LOGSYS_LEVEL_DEBUG, "quorum lost, blocking activity");
	}
	if (!cluster_is_quorate && quorate) {
		quorum_change = 1;
		log_printf(LOGSYS_LEVEL_DEBUG, "quorum regained, resuming activity");
	}

	cluster_is_quorate = quorate;
	if (cluster_is_quorate) {
		us->flags |= NODE_FLAGS_QUORATE;
	} else {
		us->flags &= ~NODE_FLAGS_QUORATE;
	}

	if (wait_for_all) {
		if (quorate) {
			update_wait_for_all_status(0);
		} else {
			update_wait_for_all_status(1);
		}
	}

	if (quorum_change) {
		quorum_callback(quorum_members, quorum_members_entries,
				cluster_is_quorate, &quorum_ringid);
	}

	LEAVE();
}

static void get_total_votes(unsigned int *totalvotes, unsigned int *current_members)
{
	unsigned int total_votes = 0;
	unsigned int cluster_members = 0;
	struct list_head *nodelist;
	struct cluster_node *node;

	ENTER();

	list_iterate(nodelist, &cluster_members_list) {
		node = list_entry(nodelist, struct cluster_node, list);
		if (node->state == NODESTATE_MEMBER) {
			cluster_members++;
			total_votes += node->votes;
		}
	}

	if (qdevice->votes) {
		total_votes += qdevice->votes;
		cluster_members++;
	}
	*totalvotes = total_votes;
	*current_members = cluster_members;

	LEAVE();
}

/*
 * Recalculate cluster quorum, set quorate and notify changes
 */
static void recalculate_quorum(int allow_decrease, int by_current_nodes)
{
	unsigned int total_votes = 0;
	unsigned int cluster_members = 0;

	ENTER();

	get_total_votes(&total_votes, &cluster_members);

	if (!by_current_nodes) {
		cluster_members = 0;
	}

	/*
	 * Keep expected_votes at the highest number of votes in the cluster
	 */
	log_printf(LOGSYS_LEVEL_DEBUG, "total_votes=%d, expected_votes=%d", total_votes, us->expected_votes);
	if (total_votes > us->expected_votes) {
		us->expected_votes = total_votes;
		votequorum_exec_send_expectedvotes_notification();
	}

	quorum = calculate_quorum(allow_decrease, cluster_members, &total_votes);
	are_we_quorate(total_votes);

	votequorum_exec_send_quorum_notification(NULL, 0L);

	LEAVE();
}

/*
 * configuration bits and pieces
 */

static int votequorum_read_nodelist_configuration(uint32_t *votes,
						  uint32_t *nodes,
						  uint32_t *expected_votes)
{
	icmap_iter_t iter;
	const char *iter_key;
	char tmp_key[ICMAP_KEYNAME_MAXLEN];
	uint32_t our_pos, node_pos;
	uint32_t nodecount = 0;
	uint32_t nodelist_expected_votes = 0;
	uint32_t node_votes = 0;
	int res = 0;

	ENTER();

	if (icmap_get_uint32("nodelist.local_node_pos", &our_pos) != CS_OK) {
		log_printf(LOGSYS_LEVEL_DEBUG,
			   "No nodelist defined or our node is not in the nodelist");
		return 0;
	}

	iter = icmap_iter_init("nodelist.node.");

	while ((iter_key = icmap_iter_next(iter, NULL, NULL)) != NULL) {

		res = sscanf(iter_key, "nodelist.node.%u.%s", &node_pos, tmp_key);
		if (res != 2) {
			continue;
		}

		if (strcmp(tmp_key, "ring0_addr") != 0) {
			continue;
		}

		nodecount++;

		snprintf(tmp_key, ICMAP_KEYNAME_MAXLEN, "nodelist.node.%u.quorum_votes", node_pos);
		if (icmap_get_uint32(tmp_key, &node_votes) != CS_OK) {
			node_votes = 1;
		}

		nodelist_expected_votes = nodelist_expected_votes + node_votes;

		if (node_pos == our_pos) {
			*votes = node_votes;
		}
	}

	*expected_votes = nodelist_expected_votes;
	*nodes = nodecount;

	icmap_iter_finalize(iter);

	LEAVE();

	return 1;
}

static int votequorum_qdevice_is_configured(uint32_t *qdevice_votes)
{
	char *qdevice_model = NULL;

	ENTER();

	if ((icmap_get_string("quorum.device.model", &qdevice_model) == CS_OK) &&
	    (strlen(qdevice_model))) {
		free(qdevice_model);
		if (icmap_get_uint32("quorum.device.votes", qdevice_votes) != CS_OK) {
			*qdevice_votes = -1;
		}
		if (icmap_get_uint32("quorum.device.timeout", &qdevice_timeout) != CS_OK) {
			qdevice_timeout = DEFAULT_QDEVICE_TIMEOUT;
		}
		update_qdevice_can_operate(1);
		return 1;
	}

	LEAVE();

	return 0;
}

#define VOTEQUORUM_READCONFIG_STARTUP 0
#define VOTEQUORUM_READCONFIG_RUNTIME 1

static char *votequorum_readconfig(int runtime)
{
	uint32_t node_votes = 0, qdevice_votes = 0;
	uint32_t node_expected_votes = 0, expected_votes = 0;
	uint32_t node_count = 0;
	int have_nodelist, have_qdevice;
	char *error = NULL;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Reading configuration (runtime: %d)", runtime);

	/*
	 * gather basic data here
	 */
	icmap_get_uint32("quorum.expected_votes", &expected_votes);
	have_nodelist = votequorum_read_nodelist_configuration(&node_votes, &node_count, &node_expected_votes);
	have_qdevice = votequorum_qdevice_is_configured(&qdevice_votes);
	icmap_get_uint8("quorum.two_node", &two_node);

	/*
	 * do config verification and enablement
	 */

	if ((!have_nodelist) && (!expected_votes)) {
		if (!runtime) {
			error = (char *)"configuration error: nodelist or quorum.expected_votes must be configured!";
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: nodelist or quorum.expected_votes must be configured!");
			log_printf(LOGSYS_LEVEL_CRIT, "will continue with current runtime data");
		}
		goto out;
	}

	/*
	 * two_node and qdevice are not compatible in the same config.
	 * try to make an educated guess of what to do
	 */

	if ((two_node) && (have_qdevice)) {
		if (!runtime) {
			error = (char *)"configuration error: two_node and quorum device cannot be configured at the same time!";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: two_node and quorum device cannot be configured at the same time!");
			if (qdevice_is_registered) {
				log_printf(LOGSYS_LEVEL_CRIT, "quorum device is registered, disabling two_node");
				two_node = 0;
			} else {
				log_printf(LOGSYS_LEVEL_CRIT, "quorum device is not registered, allowing two_node");
				update_qdevice_can_operate(0);
			}
		}
	}

	/*
	 * Enable special features
	 */
	if (!runtime) {
		if (two_node) {
			wait_for_all = 1;
		}

		icmap_get_uint8("quorum.allow_downscale", &allow_downscale);
		icmap_get_uint8("quorum.wait_for_all", &wait_for_all);
		icmap_get_uint8("quorum.auto_tie_breaker", &auto_tie_breaker);
		icmap_get_uint8("quorum.last_man_standing", &last_man_standing);
		icmap_get_uint32("quorum.last_man_standing_window", &last_man_standing_window);
	}

	/*
	 * quorum device is not compatible with last_man_standing and auto_tie_breaker
	 * neither lms or atb can be set at runtime, so there is no need to check for
	 * runtime incompatibilities, but qdevice can be configured _after_ LMS and ATB have
	 * been enabled at startup.
	 */

	if ((have_qdevice) && (last_man_standing)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with last_man_standing";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with last_man_standing");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	if ((have_qdevice) && (auto_tie_breaker)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with auto_tie_breaker";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with auto_tie_breaker");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	if ((have_qdevice) && (wait_for_all)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with wait_for_all";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with wait_for_all");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	if ((have_qdevice) && (allow_downscale)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device is not compatible with allow_downscale";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device is not compatible with allow_downscale");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * if user specifies quorum.expected_votes + quorum.device but NOT the device.votes
	 * we don't know what the quorum device should vote.
	 */

	if ((expected_votes) && (have_qdevice) && (qdevice_votes == -1)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device.votes must be specified when quorum.expected_votes is set";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes must be specified when quorum.expected_votes is set");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * if user specifies a node list with uneven votes and no device.votes
	 * we cannot autocalculate the votes
	 */

	if ((have_qdevice) &&
	    (qdevice_votes == -1) &&
	    (have_nodelist) &&
	    (node_count != node_expected_votes)) {
		if (!runtime) {
			error = (char *)"configuration error: quorum.device.votes must be specified when not all nodes votes 1";
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes must be specified when not all nodes votes 1");
			log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
			update_qdevice_can_operate(0);
		}
	}

	/*
	 * validate quorum device votes vs expected_votes
	 */

	if ((qdevice_votes > 0) && (expected_votes)) {
		int delta = expected_votes - qdevice_votes;
		if (delta < 2) {
			if (!runtime) {
				error = (char *)"configuration error: quorum.device.votes is too high or expected_votes is too low";
				goto out;
			} else {
				log_printf(LOGSYS_LEVEL_CRIT, "configuration error: quorum.device.votes is too high or expected_votes is too low");
				log_printf(LOGSYS_LEVEL_CRIT, "disabling quorum device operations");
				update_qdevice_can_operate(0);
			}
		}
	}

	/*
	 * automatically calculate device votes and adjust expected_votes from nodelist
	 */

	if ((have_qdevice) &&
	    (qdevice_votes == -1) &&
	    (!expected_votes) &&
	    (have_nodelist) &&
	    (node_count == node_expected_votes)) {
		qdevice_votes = node_expected_votes - 1;
		node_expected_votes = node_expected_votes + qdevice_votes;
	}

	/*
	 * set this node votes and expected_votes
	 */

	if (have_nodelist) {
		us->votes = node_votes;
		us->expected_votes = node_expected_votes;
	} else {
		us->votes = 1;
		icmap_get_uint32("quorum.votes", &us->votes);
	}

	if (expected_votes) {
		us->expected_votes = expected_votes;
	}

	/*
	 * set qdevice votes
	 */

	if (!have_qdevice) {
		qdevice->votes = 0;
	}

	if (qdevice_votes != -1) {
		qdevice->votes = qdevice_votes;
	}

	update_ev_barrier(us->expected_votes);
	update_two_node();
	if (wait_for_all) {
		update_wait_for_all_status(1);
	}

out:
	LEAVE();
	return error;
}

static void votequorum_refresh_config(
	int32_t event,
	const char *key_name,
	struct icmap_notify_value new_val,
	struct icmap_notify_value old_val,
	void *user_data)
{
	ENTER();

	/*
	 * Reload the configuration
	 */
	votequorum_readconfig(VOTEQUORUM_READCONFIG_RUNTIME);

	/*
	 * activate new config
	 */
	votequorum_exec_send_nodeinfo(us->node_id);
	votequorum_exec_send_nodeinfo(NODEID_QDEVICE);

	LEAVE();
}

static void votequorum_exec_add_config_notification(void)
{
	icmap_track_t icmap_track_nodelist = NULL;
	icmap_track_t icmap_track_quorum = NULL;

	ENTER();

	icmap_track_add("nodelist.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		votequorum_refresh_config,
		NULL,
		&icmap_track_nodelist);

	icmap_track_add("quorum.",
		ICMAP_TRACK_ADD | ICMAP_TRACK_DELETE | ICMAP_TRACK_MODIFY | ICMAP_TRACK_PREFIX,
		votequorum_refresh_config,
		NULL,
		&icmap_track_quorum);

	LEAVE();
}

/*
 * votequorum_exec core
 */

static int votequorum_exec_send_reconfigure(uint8_t param, unsigned int nodeid, uint32_t value)
{
	struct req_exec_quorum_reconfigure req_exec_quorum_reconfigure;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_reconfigure.nodeid = nodeid;
	req_exec_quorum_reconfigure.value = value;
	req_exec_quorum_reconfigure.param = param;
	req_exec_quorum_reconfigure._pad0 = 0;
	req_exec_quorum_reconfigure._pad1 = 0;
	req_exec_quorum_reconfigure._pad2 = 0;

	req_exec_quorum_reconfigure.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_RECONFIGURE);
	req_exec_quorum_reconfigure.header.size = sizeof(req_exec_quorum_reconfigure);

	iov[0].iov_base = (void *)&req_exec_quorum_reconfigure;
	iov[0].iov_len = sizeof(req_exec_quorum_reconfigure);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_nodeinfo(uint32_t nodeid)
{
	struct req_exec_quorum_nodeinfo req_exec_quorum_nodeinfo;
	struct iovec iov[1];
	struct cluster_node *node;
	int ret;

	ENTER();

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		return -1;
	}

	req_exec_quorum_nodeinfo.nodeid = nodeid;
	req_exec_quorum_nodeinfo.votes = node->votes;
	req_exec_quorum_nodeinfo.expected_votes = node->expected_votes;
	req_exec_quorum_nodeinfo.flags = node->flags;
	if (nodeid != NODEID_QDEVICE) {
		decode_flags(node->flags);
	}

	req_exec_quorum_nodeinfo.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_NODEINFO);
	req_exec_quorum_nodeinfo.header.size = sizeof(req_exec_quorum_nodeinfo);

	iov[0].iov_base = (void *)&req_exec_quorum_nodeinfo;
	iov[0].iov_len = sizeof(req_exec_quorum_nodeinfo);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_qdevice_reconfigure(const char *oldname, const char *newname)
{
	struct req_exec_quorum_qdevice_reconfigure req_exec_quorum_qdevice_reconfigure;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_qdevice_reconfigure.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_RECONFIGURE);
	req_exec_quorum_qdevice_reconfigure.header.size = sizeof(req_exec_quorum_qdevice_reconfigure);
	strcpy(req_exec_quorum_qdevice_reconfigure.oldname, oldname);
	strcpy(req_exec_quorum_qdevice_reconfigure.newname, newname);

	iov[0].iov_base = (void *)&req_exec_quorum_qdevice_reconfigure;
	iov[0].iov_len = sizeof(req_exec_quorum_qdevice_reconfigure);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_qdevice_reg(uint32_t operation, const char *qdevice_name_req)
{
	struct req_exec_quorum_qdevice_reg req_exec_quorum_qdevice_reg;
	struct iovec iov[1];
	int ret;

	ENTER();

	req_exec_quorum_qdevice_reg.header.id = SERVICE_ID_MAKE(VOTEQUORUM_SERVICE, MESSAGE_REQ_EXEC_VOTEQUORUM_QDEVICE_REG);
	req_exec_quorum_qdevice_reg.header.size = sizeof(req_exec_quorum_qdevice_reg);
	req_exec_quorum_qdevice_reg.operation = operation;
	strcpy(req_exec_quorum_qdevice_reg.qdevice_name, qdevice_name_req);

	iov[0].iov_base = (void *)&req_exec_quorum_qdevice_reg;
	iov[0].iov_len = sizeof(req_exec_quorum_qdevice_reg);

	ret = corosync_api->totem_mcast (iov, 1, TOTEM_AGREED);

	LEAVE();
	return ret;
}

static int votequorum_exec_send_quorum_notification(void *conn, uint64_t context)
{
	struct res_lib_votequorum_notification *res_lib_votequorum_notification;
	struct list_head *tmp;
	struct cluster_node *node;
	int cluster_members = 0;
	int i = 0;
	int size;
	char buf[sizeof(struct res_lib_votequorum_notification) + sizeof(struct votequorum_node) * (PROCESSOR_COUNT_MAX + 2)];

	ENTER();

	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		cluster_members++;
        }
	if (qdevice_is_registered) {
		cluster_members++;
	}

	size = sizeof(struct res_lib_votequorum_notification) + sizeof(struct votequorum_node) * cluster_members;

	res_lib_votequorum_notification = (struct res_lib_votequorum_notification *)&buf;
	res_lib_votequorum_notification->quorate = cluster_is_quorate;
	res_lib_votequorum_notification->node_list_entries = cluster_members;
	res_lib_votequorum_notification->context = context;
	list_iterate(tmp, &cluster_members_list) {
		node = list_entry(tmp, struct cluster_node, list);
		res_lib_votequorum_notification->node_list[i].nodeid = node->node_id;
		res_lib_votequorum_notification->node_list[i++].state = node->state;
        }
	if (qdevice_is_registered) {
		res_lib_votequorum_notification->node_list[i].nodeid = NODEID_QDEVICE;
		res_lib_votequorum_notification->node_list[i++].state = qdevice->state;
	}
	res_lib_votequorum_notification->header.id = MESSAGE_RES_VOTEQUORUM_NOTIFICATION;
	res_lib_votequorum_notification->header.size = size;
	res_lib_votequorum_notification->header.error = CS_OK;

	/* Send it to all interested parties */
	if (conn) {
		int ret = corosync_api->ipc_dispatch_send(conn, &buf, size);
		LEAVE();
		return ret;
	} else {
		struct quorum_pd *qpd;

		list_iterate(tmp, &trackers_list) {
			qpd = list_entry(tmp, struct quorum_pd, list);
			res_lib_votequorum_notification->context = qpd->tracking_context;
			corosync_api->ipc_dispatch_send(qpd->conn, &buf, size);
		}
	}

	LEAVE();

	return 0;
}

static void votequorum_exec_send_expectedvotes_notification(void)
{
	struct res_lib_votequorum_expectedvotes_notification res_lib_votequorum_expectedvotes_notification;
	struct quorum_pd *qpd;
	struct list_head *tmp;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Sending expected votes callback");

	res_lib_votequorum_expectedvotes_notification.header.id = MESSAGE_RES_VOTEQUORUM_EXPECTEDVOTES_NOTIFICATION;
	res_lib_votequorum_expectedvotes_notification.header.size = sizeof(res_lib_votequorum_expectedvotes_notification);
	res_lib_votequorum_expectedvotes_notification.header.error = CS_OK;
	res_lib_votequorum_expectedvotes_notification.expected_votes = us->expected_votes;

	list_iterate(tmp, &trackers_list) {
		qpd = list_entry(tmp, struct quorum_pd, list);
		res_lib_votequorum_expectedvotes_notification.context = qpd->tracking_context;
		corosync_api->ipc_dispatch_send(qpd->conn, &res_lib_votequorum_expectedvotes_notification,
						sizeof(struct res_lib_votequorum_expectedvotes_notification));
	}

	LEAVE();
}

static void exec_votequorum_qdevice_reconfigure_endian_convert (void *message)
{
	ENTER();

	LEAVE();
}

static void message_handler_req_exec_votequorum_qdevice_reconfigure (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_qdevice_reconfigure *req_exec_quorum_qdevice_reconfigure = message;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Received qdevice name change req from node %u [from: %s to: %s]",
		   nodeid,
		   req_exec_quorum_qdevice_reconfigure->oldname,
		   req_exec_quorum_qdevice_reconfigure->newname);

	if (!strcmp(req_exec_quorum_qdevice_reconfigure->oldname, qdevice_name)) {
		log_printf(LOGSYS_LEVEL_DEBUG, "Allowing qdevice rename");
		memset(qdevice_name, 0, VOTEQUORUM_MAX_QDEVICE_NAME_LEN);
		strcpy(qdevice_name, req_exec_quorum_qdevice_reconfigure->newname);
		/*
		 * TODO: notify qdevices about name change?
		 *       this is not relevant for now and can wait later on since
		 *       qdevices are local only and libvotequorum is not final
		 */
	}

	LEAVE();
}

static void exec_votequorum_qdevice_reg_endian_convert (void *message)
{
	struct req_exec_quorum_qdevice_reg *req_exec_quorum_qdevice_reg = message;

	ENTER();

	req_exec_quorum_qdevice_reg->operation = swab32(req_exec_quorum_qdevice_reg->operation);	

	LEAVE();
}

static void message_handler_req_exec_votequorum_qdevice_reg (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_qdevice_reg *req_exec_quorum_qdevice_reg = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	int wipe_qdevice_name = 1;
	struct cluster_node *node = NULL;
	struct list_head *tmp;
	cs_error_t error = CS_OK;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "Received qdevice op %u req from node %u [%s]",
		   req_exec_quorum_qdevice_reg->operation,
		   nodeid, req_exec_quorum_qdevice_reg->qdevice_name);

	switch(req_exec_quorum_qdevice_reg->operation)
	{
	case VOTEQUORUM_QDEVICE_OPERATION_REGISTER:
		if (nodeid != us->node_id) {
			if (!strlen(qdevice_name)) {
				log_printf(LOGSYS_LEVEL_DEBUG, "Remote qdevice name recorded");
				strcpy(qdevice_name, req_exec_quorum_qdevice_reg->qdevice_name);
			}
			LEAVE();
			return;
		}

		/*
		 * this should NEVER happen
		 */
		if (!qdevice_reg_conn) {
			log_printf(LOGSYS_LEVEL_WARNING, "Unable to determine origin of the qdevice register call!");
			LEAVE();
			return;
		}

		/*
		 * registering our own device in this case
		 */
		if (!strlen(qdevice_name)) {
			strcpy(qdevice_name, req_exec_quorum_qdevice_reg->qdevice_name);
		}

		/*
		 * check if it is our device or something else
		 */
		if ((!strncmp(req_exec_quorum_qdevice_reg->qdevice_name,
			      qdevice_name, VOTEQUORUM_MAX_QDEVICE_NAME_LEN))) {
			qdevice_is_registered = 1;
			us->flags |= NODE_FLAGS_QDEVICE;
			votequorum_exec_send_nodeinfo(NODEID_QDEVICE);
			votequorum_exec_send_nodeinfo(us->node_id);
		} else {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "A new qdevice with different name (new: %s old: %s) is trying to register!",
				   req_exec_quorum_qdevice_reg->qdevice_name, qdevice_name);
			error = CS_ERR_EXIST;
		}

		res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
		res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
		res_lib_votequorum_status.header.error = error;
		corosync_api->ipc_response_send(qdevice_reg_conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));
		qdevice_reg_conn = NULL;
		break;
	case VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER:
		list_iterate(tmp, &cluster_members_list) {
			node = list_entry(tmp, struct cluster_node, list);
			if ((node->state == NODESTATE_MEMBER) &&
			    (node->flags & NODE_FLAGS_QDEVICE)) {
				wipe_qdevice_name = 0;
			}
		}

		if (wipe_qdevice_name) {
			memset(qdevice_name, 0, VOTEQUORUM_MAX_QDEVICE_NAME_LEN);
		}

		break;
	}
	LEAVE();
}

static void exec_votequorum_nodeinfo_endian_convert (void *message)
{
	struct req_exec_quorum_nodeinfo *nodeinfo = message;

	ENTER();

	nodeinfo->nodeid = swab32(nodeinfo->nodeid);
	nodeinfo->votes = swab32(nodeinfo->votes);
	nodeinfo->expected_votes = swab32(nodeinfo->expected_votes);
	nodeinfo->flags = swab32(nodeinfo->flags);

	LEAVE();
}

static void message_handler_req_exec_votequorum_nodeinfo (
	const void *message,
	unsigned int sender_nodeid)
{
	const struct req_exec_quorum_nodeinfo *req_exec_quorum_nodeinfo = message;
	struct cluster_node *node = NULL;
	int old_votes;
	int old_expected;
	uint32_t old_flags;
	nodestate_t old_state;
	int new_node = 0;
	int allow_downgrade = 0;
	int by_node = 0;
	unsigned int nodeid = req_exec_quorum_nodeinfo->nodeid;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got nodeinfo message from cluster node %u", sender_nodeid);
	log_printf(LOGSYS_LEVEL_DEBUG, "nodeinfo message[%u]: votes: %d, expected: %d flags: %d",
					nodeid,
					req_exec_quorum_nodeinfo->votes,
					req_exec_quorum_nodeinfo->expected_votes,
					req_exec_quorum_nodeinfo->flags);

	if (nodeid != NODEID_QDEVICE) {
		decode_flags(req_exec_quorum_nodeinfo->flags);
	}

	node = find_node_by_nodeid(nodeid);
	if (!node) {
		node = allocate_node(nodeid);
		new_node = 1;
	}
	if (!node) {
		corosync_api->error_memory_failure();
		LEAVE();
		return;
	}

	if (new_node) {
		old_votes = 0;
		old_expected = 0;
		old_state = NODESTATE_DEAD;
		old_flags = 0;
	} else {
		old_votes = node->votes;
		old_expected = node->expected_votes;
		old_state = node->state;
		old_flags = node->flags;
	}

	/* Update node state */
	node->flags = req_exec_quorum_nodeinfo->flags;

	if (nodeid != NODEID_QDEVICE) {
		node->votes = req_exec_quorum_nodeinfo->votes;
	} else {
		if ((!cluster_is_quorate) &&
		    (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_QUORATE)) {
			node->votes = req_exec_quorum_nodeinfo->votes;
		} else {
			node->votes = max(node->votes, req_exec_quorum_nodeinfo->votes);
		}
	}

	if (node->flags & NODE_FLAGS_LEAVING) {
		node->state = NODESTATE_LEAVING;
		allow_downgrade = 1;
		by_node = 1;
	} else {
		if (nodeid != NODEID_QDEVICE) {
			node->state = NODESTATE_MEMBER;
		} else {
			/*
			 * qdevice status is only local to the node
			 */
			node->state = old_state;
		}
	}

	if (nodeid != NODEID_QDEVICE) {
		if ((!cluster_is_quorate) &&
		    (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_QUORATE)) {
			allow_downgrade = 1;
			us->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
		}

		if (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_QUORATE) {
			node->expected_votes = req_exec_quorum_nodeinfo->expected_votes;
		} else {
			node->expected_votes = us->expected_votes;
		}

		if ((last_man_standing) && (req_exec_quorum_nodeinfo->votes > 1)) {
			log_printf(LOGSYS_LEVEL_WARNING, "Last Man Standing feature is supported only when all"
							 "cluster nodes votes are set to 1. Disabling LMS.");
			last_man_standing = 0;
			if (last_man_standing_timer_set) {
				corosync_api->timer_delete(last_man_standing_timer);
				last_man_standing_timer_set = 0;
			}
		}
	}

	if (new_node ||
	    nodeid == NODEID_QDEVICE ||
	    req_exec_quorum_nodeinfo->flags & NODE_FLAGS_FIRST || 
	    old_votes != node->votes ||
	    old_expected != node->expected_votes ||
	    old_flags != node->flags ||
	    old_state != node->state) {
		recalculate_quorum(allow_downgrade, by_node);
	}

	if ((wait_for_all) &&
	    (!(req_exec_quorum_nodeinfo->flags & NODE_FLAGS_WFASTATUS)) &&
	    (req_exec_quorum_nodeinfo->flags & NODE_FLAGS_QUORATE)) {
		update_wait_for_all_status(0);
	}

	LEAVE();
}

static void exec_votequorum_reconfigure_endian_convert (void *message)
{
	struct req_exec_quorum_reconfigure *reconfigure = message;

	ENTER();

	reconfigure->nodeid = swab32(reconfigure->nodeid);
	reconfigure->value = swab32(reconfigure->value);

	LEAVE();
}

static void message_handler_req_exec_votequorum_reconfigure (
	const void *message,
	unsigned int nodeid)
{
	const struct req_exec_quorum_reconfigure *req_exec_quorum_reconfigure = message;
	struct cluster_node *node;
	struct list_head *nodelist;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got reconfigure message from cluster node %u for %u",
					nodeid, req_exec_quorum_reconfigure->nodeid);

	switch(req_exec_quorum_reconfigure->param)
	{
	case VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES:
		list_iterate(nodelist, &cluster_members_list) {
			node = list_entry(nodelist, struct cluster_node, list);
			if (node->state == NODESTATE_MEMBER) {
				node->expected_votes = req_exec_quorum_reconfigure->value;
			}
		}
		votequorum_exec_send_expectedvotes_notification();
		update_ev_barrier(req_exec_quorum_reconfigure->value);
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	case VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES:
		node = find_node_by_nodeid(req_exec_quorum_reconfigure->nodeid);
		if (!node) {
			LEAVE();
			return;
		}
		node->votes = req_exec_quorum_reconfigure->value;
		recalculate_quorum(1, 0);  /* Allow decrease */
		break;

	}

	LEAVE();
}

static int votequorum_exec_exit_fn (void)
{
	int ret = 0;

	ENTER();

	/*
	 * tell the other nodes we are leaving
	 */

	if (allow_downscale) {
		us->flags |= NODE_FLAGS_LEAVING;
		ret = votequorum_exec_send_nodeinfo(us->node_id);
	}

	/*
	 * clean up our internals
	 */

	/*
	 * free the node list and qdevice
	 */

	list_init(&cluster_members_list);
	qdevice = NULL;
	us = NULL;
	memset(cluster_nodes, 0, sizeof(cluster_nodes));

	/*
	 * clean the tracking list
	 */

	list_init(&trackers_list);

	LEAVE();
	return ret;
}

static char *votequorum_exec_init_fn (struct corosync_api_v1 *api)
{
	char *error = NULL;

	ENTER();

	list_init(&cluster_members_list);
	list_init(&trackers_list);

	/*
	 * Allocate a cluster_node for qdevice
	 */
	qdevice = allocate_node(NODEID_QDEVICE);
	if (!qdevice) {
		LEAVE();
		return ((char *)"Could not allocate node.");
	}
	qdevice->state = NODESTATE_DEAD;
	qdevice->votes = 0;
	memset(qdevice_name, 0, VOTEQUORUM_MAX_QDEVICE_NAME_LEN);

	/*
	 * Allocate a cluster_node for us
	 */
	us = allocate_node(corosync_api->totem_nodeid_get());
	if (!us) {
		LEAVE();
		return ((char *)"Could not allocate node.");
	}

	icmap_set_uint32("runtime.votequorum.this_node_id", us->node_id);

	us->state = NODESTATE_MEMBER;
	us->votes = 1;
	us->flags |= NODE_FLAGS_FIRST;

	error = votequorum_readconfig(VOTEQUORUM_READCONFIG_STARTUP);
	if (error) {
		return error;
	}
	recalculate_quorum(0, 0);

	/*
	 * Listen for changes
	 */
	votequorum_exec_add_config_notification();

	/*
	 * Start us off with one node
	 */
	votequorum_exec_send_nodeinfo(us->node_id);

	LEAVE();

	return (NULL);
}

/*
 * votequorum service core
 */

static void votequorum_last_man_standing_timer_fn(void *arg)
{
	ENTER();

	last_man_standing_timer_set = 0;
	if (cluster_is_quorate) {
		recalculate_quorum(1,1);
	}

	LEAVE();
}

static void votequorum_confchg_fn (
	enum totem_configuration_type configuration_type,
	const unsigned int *member_list, size_t member_list_entries,
	const unsigned int *left_list, size_t left_list_entries,
	const unsigned int *joined_list, size_t joined_list_entries,
	const struct memb_ring_id *ring_id)
{
	int i;
	struct cluster_node *node;

	ENTER();

	if (member_list_entries > 1) {
		us->flags &= ~NODE_FLAGS_FIRST;
	}

	if (left_list_entries) {
		for (i = 0; i< left_list_entries; i++) {
			node = find_node_by_nodeid(left_list[i]);
			if (node) {
				node->state = NODESTATE_DEAD;
			}
		}
	}

	if (last_man_standing) {
		if (((member_list_entries >= quorum) && (left_list_entries)) ||
		    ((member_list_entries <= quorum) && (auto_tie_breaker) && (check_low_node_id_partition() == 1))) {
			if (last_man_standing_timer_set) {
				corosync_api->timer_delete(last_man_standing_timer);
				last_man_standing_timer_set = 0;
			}
			corosync_api->timer_add_duration((unsigned long long)last_man_standing_window*1000000,
							 NULL, votequorum_last_man_standing_timer_fn,
							 &last_man_standing_timer);
			last_man_standing_timer_set = 1;
		}
	}

	if (member_list_entries) {
		memcpy(quorum_members, member_list, sizeof(unsigned int) * member_list_entries);
		quorum_members_entries = member_list_entries;
		votequorum_exec_send_nodeinfo(us->node_id);
		votequorum_exec_send_nodeinfo(NODEID_QDEVICE);
		if (strlen(qdevice_name)) {
			votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_REGISTER,
							 qdevice_name);
		}
	}

	memcpy(&quorum_ringid, ring_id, sizeof(*ring_id));

	if (left_list_entries) {
		recalculate_quorum(0, 0);
	}

	if (configuration_type == TOTEM_CONFIGURATION_REGULAR) {
		quorum_callback(quorum_members, quorum_members_entries,
				cluster_is_quorate, &quorum_ringid);
	}

	LEAVE();
}


char *votequorum_init(struct corosync_api_v1 *api,
	quorum_set_quorate_fn_t q_set_quorate_fn)
{
	char *error;

	ENTER();

	if (q_set_quorate_fn == NULL) {
		return ((char *)"Quorate function not set");
	}

	corosync_api = api;
	quorum_callback = q_set_quorate_fn;

	error = corosync_service_link_and_init(corosync_api,
		&votequorum_service[0]);
	if (error) {
		return (error);
	}

	LEAVE();

	return (NULL);
}

/*
 * Library Handler init/fini
 */

static int quorum_lib_init_fn (void *conn)
{
	struct quorum_pd *pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	list_init (&pd->list);
	pd->conn = conn;

	LEAVE();
	return (0);
}

static int quorum_lib_exit_fn (void *conn)
{
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();

	if (quorum_pd->tracking_enabled) {
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	}

	LEAVE();

	return (0);
}

/*
 * library internal functions
 */

static void qdevice_timer_fn(void *arg)
{
	ENTER();

	if ((!qdevice_is_registered) ||
	    (qdevice->state == NODESTATE_DEAD) ||
	    (!qdevice_timer_set)) {
		LEAVE();
		return;
	}

	qdevice->state = NODESTATE_DEAD;
	us->flags &= ~NODE_FLAGS_QDEVICE_STATE;
	log_printf(LOGSYS_LEVEL_INFO, "lost contact with quorum device %s", qdevice_name);
	votequorum_exec_send_nodeinfo(us->node_id);

	qdevice_timer_set = 0;

	LEAVE();
}

/*
 * Library Handler Functions
 */

static void message_handler_req_lib_votequorum_getinfo (void *conn, const void *message)
{
	const struct req_lib_votequorum_getinfo *req_lib_votequorum_getinfo = message;
	struct res_lib_votequorum_getinfo res_lib_votequorum_getinfo;
	struct cluster_node *node;
	unsigned int highest_expected = 0;
	unsigned int total_votes = 0;
	cs_error_t error = CS_OK;

	ENTER();

	log_printf(LOGSYS_LEVEL_DEBUG, "got getinfo request on %p for node %u", conn, req_lib_votequorum_getinfo->nodeid);

	node = find_node_by_nodeid(req_lib_votequorum_getinfo->nodeid);
	if (node) {
		struct cluster_node *iternode;
		struct list_head *nodelist;

		list_iterate(nodelist, &cluster_members_list) {
			iternode = list_entry(nodelist, struct cluster_node, list);

			if (iternode->state == NODESTATE_MEMBER) {
				highest_expected =
					max(highest_expected, iternode->expected_votes);
				total_votes += iternode->votes;
			}
		}

		if (((qdevice_is_registered) && (qdevice->state == NODESTATE_MEMBER)) ||
		    ((node->flags & NODE_FLAGS_QDEVICE) && (node->flags & NODE_FLAGS_QDEVICE_STATE))) {
			total_votes += qdevice->votes;
		}

		res_lib_votequorum_getinfo.state = node->state;
		res_lib_votequorum_getinfo.votes = node->votes;
		res_lib_votequorum_getinfo.expected_votes = node->expected_votes;
		res_lib_votequorum_getinfo.highest_expected = highest_expected;

		res_lib_votequorum_getinfo.quorum = quorum;
		res_lib_votequorum_getinfo.total_votes = total_votes;
		res_lib_votequorum_getinfo.flags = 0;
		res_lib_votequorum_getinfo.nodeid = node->node_id;

		if (two_node) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_TWONODE;
		}
		if (cluster_is_quorate) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_FLAG_QUORATE;
		}
		if (wait_for_all) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_WAIT_FOR_ALL;
		}
		if (last_man_standing) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_LAST_MAN_STANDING;
		}
		if (auto_tie_breaker) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_AUTO_TIE_BREAKER;
		}
		if (allow_downscale) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_LEAVE_REMOVE;
		}
		if (node->flags & NODE_FLAGS_QDEVICE) {
			res_lib_votequorum_getinfo.flags |= VOTEQUORUM_INFO_QDEVICE;
		}
	} else {
		error = CS_ERR_NOT_EXIST;
	}

	res_lib_votequorum_getinfo.header.size = sizeof(res_lib_votequorum_getinfo);
	res_lib_votequorum_getinfo.header.id = MESSAGE_RES_VOTEQUORUM_GETINFO;
	res_lib_votequorum_getinfo.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_getinfo, sizeof(res_lib_votequorum_getinfo));
	log_printf(LOGSYS_LEVEL_DEBUG, "getinfo response error: %d", error);

	LEAVE();
}

static void message_handler_req_lib_votequorum_setexpected (void *conn, const void *message)
{
	const struct req_lib_votequorum_setexpected *req_lib_votequorum_setexpected = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;
	unsigned int newquorum;
	unsigned int total_votes;
	uint8_t allow_downscale_status = 0;

	ENTER();

	allow_downscale_status = allow_downscale;
	allow_downscale = 0;

	/*
	 * Validate new expected votes
	 */
	newquorum = calculate_quorum(1, req_lib_votequorum_setexpected->expected_votes, &total_votes);
	allow_downscale = allow_downscale_status;
	if (newquorum < total_votes / 2 ||
	    newquorum > total_votes) {
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_EXPECTED_VOTES, us->node_id,
					 req_lib_votequorum_setexpected->expected_votes);

error_exit:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_setvotes (void *conn, const void *message)
{
	const struct req_lib_votequorum_setvotes *req_lib_votequorum_setvotes = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct cluster_node *node;
	unsigned int newquorum;
	unsigned int total_votes;
	unsigned int saved_votes;
	cs_error_t error = CS_OK;
	unsigned int nodeid;

	ENTER();

	nodeid = req_lib_votequorum_setvotes->nodeid;
	node = find_node_by_nodeid(nodeid);
	if (!node) {
		error = CS_ERR_NAME_NOT_FOUND;
		goto error_exit;
	}

	/*
	 * Check votes is valid
	 */
	saved_votes = node->votes;
	node->votes = req_lib_votequorum_setvotes->votes;

	newquorum = calculate_quorum(1, 0, &total_votes);

	if (newquorum < total_votes / 2 ||
	    newquorum > total_votes) {
		node->votes = saved_votes;
		error = CS_ERR_INVALID_PARAM;
		goto error_exit;
	}

	votequorum_exec_send_reconfigure(VOTEQUORUM_RECONFIG_PARAM_NODE_VOTES, nodeid,
					 req_lib_votequorum_setvotes->votes);

error_exit:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstart (void *conn,
							   const void *message)
{
	const struct req_lib_votequorum_trackstart *req_lib_votequorum_trackstart = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);

	ENTER();
	/*
	 * If an immediate listing of the current cluster membership
	 * is requested, generate membership list
	 */
	if (req_lib_votequorum_trackstart->track_flags & CS_TRACK_CURRENT ||
	    req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES) {
		log_printf(LOGSYS_LEVEL_DEBUG, "sending initial status to %p", conn);
		votequorum_exec_send_quorum_notification(conn, req_lib_votequorum_trackstart->context);
	}

	/*
	 * Record requests for tracking
	 */
	if (req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES ||
	    req_lib_votequorum_trackstart->track_flags & CS_TRACK_CHANGES_ONLY) {

		quorum_pd->track_flags = req_lib_votequorum_trackstart->track_flags;
		quorum_pd->tracking_enabled = 1;
		quorum_pd->tracking_context = req_lib_votequorum_trackstart->context;

		list_add (&quorum_pd->list, &trackers_list);
	}

	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = CS_OK;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_trackstop (void *conn,
							  const void *message)
{
	struct res_lib_votequorum_status res_lib_votequorum_status;
	struct quorum_pd *quorum_pd = (struct quorum_pd *)corosync_api->ipc_private_data_get (conn);
	int error = CS_OK;

	ENTER();

	if (quorum_pd->tracking_enabled) {
		error = CS_OK;
		quorum_pd->tracking_enabled = 0;
		list_del (&quorum_pd->list);
		list_init (&quorum_pd->list);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_register (void *conn,
								 const void *message)
{
	const struct req_lib_votequorum_qdevice_register *req_lib_votequorum_qdevice_register = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (!qdevice_can_operate) {
		log_printf(LOGSYS_LEVEL_INFO, "Registration of quorum device is disabled by incorrect corosync.conf. See logs for more information");
		error = CS_ERR_ACCESS;
		goto out;
	}

	if (qdevice_is_registered) {
		if ((!strncmp(req_lib_votequorum_qdevice_register->name,
		     qdevice_name, VOTEQUORUM_MAX_QDEVICE_NAME_LEN))) {
			goto out;
		} else {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "A new qdevice with different name (new: %s old: %s) is trying to re-register!",
				   req_lib_votequorum_qdevice_register->name, qdevice_name);
			error = CS_ERR_EXIST;
			goto out;
		}
	} else {
		if (qdevice_reg_conn != NULL) {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "Registration request already in progress");
			error = CS_ERR_TRY_AGAIN;
			goto out;
		}
		qdevice_reg_conn = conn;
		if (votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_REGISTER,
						     req_lib_votequorum_qdevice_register->name) != 0) {
			log_printf(LOGSYS_LEVEL_WARNING,
				   "Unable to send qdevice registration request to cluster");
			error = CS_ERR_TRY_AGAIN;
			qdevice_reg_conn = NULL;
		} else {
			LEAVE();
			return;
		}
	}

out:

	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_unregister (void *conn,
								   const void *message)
{
	const struct req_lib_votequorum_qdevice_unregister *req_lib_votequorum_qdevice_unregister = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (qdevice_is_registered) {
		if (strncmp(req_lib_votequorum_qdevice_unregister->name, qdevice_name, VOTEQUORUM_MAX_QDEVICE_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}
		if (qdevice_timer_set) {
			corosync_api->timer_delete(qdevice_timer);
			qdevice_timer_set = 0;
		}
		qdevice_is_registered = 0;
		us->flags &= ~NODE_FLAGS_QDEVICE;
		us->flags &= ~NODE_FLAGS_QDEVICE_STATE;
		qdevice->state = NODESTATE_DEAD;
		votequorum_exec_send_nodeinfo(us->node_id);
		votequorum_exec_send_qdevice_reg(VOTEQUORUM_QDEVICE_OPERATION_UNREGISTER,
						 req_lib_votequorum_qdevice_unregister->name);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_update (void *conn,
							       const void *message)
{
	const struct req_lib_votequorum_qdevice_update *req_lib_votequorum_qdevice_update = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (qdevice_is_registered) {
		if (strncmp(req_lib_votequorum_qdevice_update->oldname, qdevice_name, VOTEQUORUM_MAX_QDEVICE_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}
		votequorum_exec_send_qdevice_reconfigure(req_lib_votequorum_qdevice_update->oldname,
							 req_lib_votequorum_qdevice_update->newname);
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_poll (void *conn,
							     const void *message)
{
	const struct req_lib_votequorum_qdevice_poll *req_lib_votequorum_qdevice_poll = message;
	struct res_lib_votequorum_status res_lib_votequorum_status;
	cs_error_t error = CS_OK;

	ENTER();

	if (!qdevice_can_operate) {
		error = CS_ERR_ACCESS;
		goto out;
	}

	if (qdevice_is_registered) {
		if (strncmp(req_lib_votequorum_qdevice_poll->name, qdevice_name, VOTEQUORUM_MAX_QDEVICE_NAME_LEN)) {
			error = CS_ERR_INVALID_PARAM;
			goto out;
		}
		if (qdevice_timer_set) {
			corosync_api->timer_delete(qdevice_timer);
			qdevice_timer_set = 0;
		}

		if (req_lib_votequorum_qdevice_poll->state) {
			if (qdevice->state == NODESTATE_DEAD) {
				qdevice->state = NODESTATE_MEMBER;
				us->flags |= NODE_FLAGS_QDEVICE_STATE;
				votequorum_exec_send_nodeinfo(us->node_id);
			}
		} else {
			if (qdevice->state == NODESTATE_MEMBER) {
				qdevice->state = NODESTATE_DEAD;
				us->flags &= ~NODE_FLAGS_QDEVICE_STATE;
				votequorum_exec_send_nodeinfo(us->node_id);
			}
		}

		corosync_api->timer_add_duration((unsigned long long)qdevice_timeout*1000000, qdevice,
						 qdevice_timer_fn, &qdevice_timer);
		qdevice_timer_set = 1;
	} else {
		error = CS_ERR_NOT_EXIST;
	}

out:
	res_lib_votequorum_status.header.size = sizeof(res_lib_votequorum_status);
	res_lib_votequorum_status.header.id = MESSAGE_RES_VOTEQUORUM_STATUS;
	res_lib_votequorum_status.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_status, sizeof(res_lib_votequorum_status));

	LEAVE();
}

static void message_handler_req_lib_votequorum_qdevice_getinfo (void *conn,
								const void *message)
{
	const struct req_lib_votequorum_qdevice_getinfo *req_lib_votequorum_qdevice_getinfo = message;
	struct res_lib_votequorum_qdevice_getinfo res_lib_votequorum_qdevice_getinfo;
	cs_error_t error = CS_OK;
	struct cluster_node *node;
	uint32_t nodeid = req_lib_votequorum_qdevice_getinfo->nodeid;

	ENTER();

	if ((nodeid != us->node_id) &&
	    (nodeid != NODEID_QDEVICE)) {
		node = find_node_by_nodeid(req_lib_votequorum_qdevice_getinfo->nodeid);
		if ((node) &&
		    (node->flags & NODE_FLAGS_QDEVICE)) {
			int state = 0;

			if (node->flags & NODE_FLAGS_QDEVICE_STATE) {
				state = 1;
			}
			log_printf(LOGSYS_LEVEL_DEBUG, "got qdevice_getinfo node %u state %d", nodeid, state);
			res_lib_votequorum_qdevice_getinfo.votes = qdevice->votes;
			if (state) {
				res_lib_votequorum_qdevice_getinfo.state = 1;
			} else {
				res_lib_votequorum_qdevice_getinfo.state = 0;
			}
			strcpy(res_lib_votequorum_qdevice_getinfo.name, qdevice_name);
		} else {
			error = CS_ERR_NOT_EXIST;
		}
	} else {
		if (qdevice_is_registered) {
			log_printf(LOGSYS_LEVEL_DEBUG, "got qdevice_getinfo node %u state %d", us->node_id, qdevice->state);
			res_lib_votequorum_qdevice_getinfo.votes = qdevice->votes;
			if (qdevice->state == NODESTATE_MEMBER) {
				res_lib_votequorum_qdevice_getinfo.state = 1;
			} else {
				res_lib_votequorum_qdevice_getinfo.state = 0;
			}
			strcpy(res_lib_votequorum_qdevice_getinfo.name, qdevice_name);
		} else {
			error = CS_ERR_NOT_EXIST;
		}
	}

	res_lib_votequorum_qdevice_getinfo.header.size = sizeof(res_lib_votequorum_qdevice_getinfo);
	res_lib_votequorum_qdevice_getinfo.header.id = MESSAGE_RES_VOTEQUORUM_GETINFO;
	res_lib_votequorum_qdevice_getinfo.header.error = error;
	corosync_api->ipc_response_send(conn, &res_lib_votequorum_qdevice_getinfo, sizeof(res_lib_votequorum_qdevice_getinfo));

	LEAVE();
}
