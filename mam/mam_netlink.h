/** \file mam_netlink.h
 *	Netlink functions
 */

#if !defined(__MAM_NETLINK_H__) && defined(HAVE_LIBNL)
#define __MAM_NETLINK_H__

#include "mptcp_netlink_parser.h"
#include <event2/bufferevent.h>

void netlink_readcb(struct bufferevent*, void*);
int configure_netlink(void);
void shutdown_netlink(void);
int create_new_flow(struct mptcp_flow_info *flow);

#endif /* __MAM_NETLINK_H__ */
