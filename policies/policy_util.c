/** \file policy_util.c
 *
 *  \copyright Copyright 2013-2015 Philipp Schmidt, Theresa Enghardt, and Mirko Palmer.
 *  All rights reserved. This project is released under the New BSD License.
 */

#include "policy_util.h"

#include "clib/dlog.h"

#ifndef MAM_POLICY_UTIL_NOISY_DEBUG0
#define MAM_POLICY_UTIL_NOISY_DEBUG0 0
#endif

#ifndef MAM_POLICY_UTIL_NOISY_DEBUG1
#define MAM_POLICY_UTIL_NOISY_DEBUG1 1
#endif

#ifndef MAM_POLICY_UTIL_NOISY_DEBUG2
#define MAM_POLICY_UTIL_NOISY_DEBUG2 0
#endif


int mampol_get_socketopt(struct socketopt *list, int level, int optname, socklen_t *optlen, void *optval)
{
	struct socketopt *current = list;
	int ret = -1;

	while (current != NULL)
	{
		if (current->level == level && current->optname == optname)
		{
			if (current->optval != NULL && optval != NULL)
			{
				*optlen = current->optlen;
				memcpy(optval, current->optval, current->optlen);
			}
			ret = 0;
		}
		current = current->next;
	}
	return ret;
}

void print_pfx_addr (gpointer element, gpointer data)
{
	struct src_prefix_list *pfx = element;
	char addr_str[INET6_ADDRSTRLEN+1]; /** String for debug / error printing */

	/* Print first address of this prefix */
	if (pfx->family == AF_INET)
	{
		inet_ntop(AF_INET, &( ((struct sockaddr_in *) (pfx->if_addrs->addr))->sin_addr ), addr_str, sizeof(addr_str));
		printf("\n\t\t%s", addr_str);
	}
	else if (pfx->family == AF_INET6)
	{
		inet_ntop(AF_INET6, &( ((struct sockaddr_in6 *) (pfx->if_addrs->addr))->sin6_addr ), addr_str, sizeof(addr_str));
		printf("\n\t\t%s", addr_str);
	}

	/* Print policy info if available */
	if (pfx->policy_info != NULL)
		print_policy_info((void*) pfx->policy_info);
}

void make_v4v6_enabled_lists (GSList *baselist, GSList **v4list, GSList **v6list)
{
	printf("Configured addresses:");
	printf("\n\tAF_INET: ");
	filter_prefix_list (baselist, v4list, PFX_ENABLED, NULL, AF_INET, NULL);
	if (*v4list != NULL)
		g_slist_foreach(*v4list, &print_pfx_addr, NULL);
	else
		printf("\n\t\t(none)");

	printf("\n\tAF_INET6: ");
	filter_prefix_list (baselist, v6list, PFX_ENABLED, NULL, AF_INET6, NULL);
	if (*v6list != NULL)
		g_slist_foreach(*v6list, &print_pfx_addr, NULL);
	else
		printf("\n\t\t(none)");
}

void set_bind_sa(request_context_t *rctx, struct src_prefix_list *chosen, strbuf_t *sb)
{
	if(sb != NULL)
	{
		strbuf_printf(sb, "\n\tSet src=");
		_muacc_print_sockaddr(sb, chosen->if_addrs->addr, chosen->if_addrs->addr_len);
	}
	
	rctx->ctx->bind_sa_suggested = _muacc_clone_sockaddr(chosen->if_addrs->addr, chosen->if_addrs->addr_len);
	rctx->ctx->bind_sa_suggested_len = chosen->if_addrs->addr_len;
	rctx->ctx->domain = chosen->family;
}

void _set_bind_sa(request_context_t *rctx, struct sockaddr *addr, strbuf_t *sb)
{
	strbuf_printf(sb, "\n\tSet src=");
	_muacc_print_sockaddr(sb, addr, sizeof(struct sockaddr));

	rctx->ctx->bind_sa_suggested = _muacc_clone_sockaddr(addr, sizeof(struct sockaddr));
	rctx->ctx->bind_sa_suggested_len = sizeof(struct sockaddr);
}


void print_addrinfo_response (struct addrinfo *res)
{
	strbuf_t sb;
	strbuf_init(&sb);

	struct addrinfo *item = res;
	while (item != NULL)
	{
		strbuf_printf(&sb, "\t");
		if (item->ai_family == AF_INET)
			_muacc_print_sockaddr(&sb, item->ai_addr, sizeof(struct sockaddr_in));
		else if (item->ai_family == AF_INET6)
			_muacc_print_sockaddr(&sb, item->ai_addr, sizeof(struct sockaddr_in6));

		strbuf_printf(&sb, "\n");
		item = item->ai_next;
	}

	printf("%s\n", strbuf_export(&sb));
	strbuf_release(&sb);
}

void *lookup_prefix_info(struct src_prefix_list *prefix, const void *key)
{
	if (prefix == NULL || key == NULL)
	{
		DLOG(MAM_POLICY_UTIL_NOISY_DEBUG1, "Warning: Tried to look up info for NULL prefix or NULL key\n");
		return NULL;
	}

	void *value = NULL;
	if (prefix->policy_set_dict != NULL)
	{
		value = g_hash_table_lookup(prefix->policy_set_dict, key);
		if (value != NULL)
		{
			DLOG(MAM_POLICY_UTIL_NOISY_DEBUG2, "Found key %s in prefix policy_set_dict\n", (char* )key);
			return value;
		}
	}
	if (prefix->measure_dict != NULL)
	{
		value = g_hash_table_lookup(prefix->measure_dict, key);
		if (value != NULL)
		{
			DLOG(MAM_POLICY_UTIL_NOISY_DEBUG2, "Found key %s in prefix measure_dict\n", (char *) key);
			return value;
		}
	}
	if (prefix->iface != NULL && prefix->iface->policy_set_dict != NULL)
	{
		value = g_hash_table_lookup(prefix->iface->policy_set_dict, key);
		if (value != NULL)
		{
			DLOG(MAM_POLICY_UTIL_NOISY_DEBUG2, "Found key %s in iface policy_set_dict\n", (char *) key);
			return value;
		}
	}
	if (prefix->iface != NULL && prefix->iface->measure_dict != NULL)
	{
		value = g_hash_table_lookup(prefix->iface->measure_dict, key);
		if (value != NULL)
		{
			DLOG(MAM_POLICY_UTIL_NOISY_DEBUG2, "Found key %s in iface measure_dict\n", (char *) key);
			return value;
		}
	}
	return value;
}
