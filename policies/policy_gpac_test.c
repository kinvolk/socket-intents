/** \file policy_gpac.c
 *  \brief Example policy to illustrate how gpac works with the MUACC framework
 *
 *  \copyright Copyright 2013-2015 Patrick Kutter, Philipp Schmidt and Theresa Enghardt.
 *  All rights reserved. This project is released under the New BSD License.
 *
 *  Policy_info: Whether interface has been specified as default in the config file
 *               (e.g. set default = 1 in the prefix statement)
 *  Behavior:
 *  Getaddrinfo - Resolve names using the default dns_base from the MAM context
 *  Connect     - Choose the default interface if available
 */
//based on policy_sample.c

#include "policy.h"
#include "policy_util.h"

/** Policy-specific per-prefix data structure that contains additional information */
//struct sample_info {
//	int is_default;
//};

struct intents_info {
    int minfilesize;
    int maxfilesize;
	enum intent_category	category;
	char					*category_string;
	int						is_default;
};

/** List of enabled addresses for each address family */
GSList *in4_enabled = NULL;
GSList *in6_enabled = NULL;

void set_sa(request_context_t *rctx, enum intent_category given, int filesize, strbuf_t *sb);

/** Helper to set the policy information for each prefix
 *  Here, check if this prefix has been configured as default
 *  and parse the category information from config file
 */
void set_policy_info(gpointer elem, gpointer data)
{
	struct src_prefix_list *spl = elem;

	struct intents_info *new = malloc(sizeof(struct intents_info));
	new->is_default = 0;

	if (spl->policy_set_dict != NULL)
	{
		gpointer value = NULL;

		// parse the config file for arguments
		if ((value = g_hash_table_lookup(spl->policy_set_dict, "category")) != NULL)
		{
			enum intent_category cat = -1;
			if (strcmp(value, "bulktransfer") == 0)
				cat = INTENT_BULKTRANSFER;
			else if (strcmp(value, "query") == 0)
				cat = INTENT_QUERY;
			else if (strcmp(value, "controltraffic") == 0)
				cat = INTENT_CONTROLTRAFFIC;
			else if (strcmp(value, "keepalives") == 0)
				cat = INTENT_KEEPALIVES;
			else if (strcmp(value, "stream") == 0)
				cat = INTENT_STREAM;
			else
				printf("WARNING: Cannot set invalid category %s\n", (char *)value);

			if (cat >= 0 && cat <= INTENT_STREAM)
			{
				/* found valid category in config file */
				new->category = cat;
				asprintf(&(new->category_string), "%s", (char *) value);
			}
		}
		// check for filesize restrictions on this interface, in none found minsize =-1 and maxsize = -1
		if ((value = g_hash_table_lookup(spl->policy_set_dict, "minfilesize")) != NULL)
            new->minfilesize= atoi(value);
            else
                new->minfilesize = -1;
        if ((value = g_hash_table_lookup(spl->policy_set_dict, "maxfilesize")) != NULL)
            new->maxfilesize= atoi(value);
            else
                new->maxfilesize=-1;
		// set default interface
		if (((value = g_hash_table_lookup(spl->policy_set_dict, "default")) != NULL) && value )
			new->is_default = 1;
	}
	spl->policy_info = new;
}

/** Helper to print additional information given to the policy
 */
void print_policy_info(void *policy_info)
{
	struct intents_info *info = policy_info;
	if (info->is_default)
		printf("\n\t policy contains default interface");
    if (info->category_string)
        printf(" \n\t policy information for category: %s ", info->category_string);
    if (info->maxfilesize)
        printf("\n\t maximumfilesize info: %i", info->maxfilesize);
    if (info->minfilesize)
        printf("\n\t minfilesize info: %i \n", info->minfilesize);
}

/** Free the policy data structures */
void freepolicyinfo(gpointer elem, gpointer data)
{
	struct src_prefix_list *spl = elem;

	if (spl->policy_info != NULL)
            free(spl->policy_info);
    spl->policy_info = NULL;


}

/* Set the matching source address for a given category and/or filesize */
void set_sa(request_context_t *rctx, enum intent_category given, int filesize, strbuf_t *sb)
{
	GSList *spl = NULL;
	struct src_prefix_list *cur = NULL;
	struct intents_info *info = NULL;
	struct src_prefix_list *defaultaddr = NULL;

	strbuf_printf(sb, "\n \t set_sa() called");

	if (rctx->ctx->domain == AF_INET)
		spl = in4_enabled;
	else if (rctx->ctx->domain == AF_INET6)
		spl = in6_enabled;

	while (spl != NULL)
	{
		cur = spl->data;
		info = (struct intents_info *)cur->policy_info;
		//if no minfilesize is set in config it is set to 0
		if( info != NULL)
        {
                if (info->category == given && (info->minfilesize) <= filesize && filesize <= (info->maxfilesize))
                {
                    /* Category and filesize matches. Set source address */
                    set_bind_sa(rctx, cur, sb);
                    strbuf_printf(sb, "\n \t found suitable interface for category %s (%d)", info->category_string, given);
                    break;
                }


            if (info->is_default)
            {
                /* Configured as default. Store for fallback */
                strbuf_printf(sb, "\n \t setting this source address as default");
                defaultaddr = cur;
            }
            spl = spl->next;
        }
	}
	if (spl == NULL)
	{
		/* No suitable address for this category was found */
		if (given >= 0 && given <= INTENT_STREAM)
			strbuf_printf(sb, "\n\tDid not find a suitable src address for category %d", given);
		if (defaultaddr != NULL)
		{
			set_bind_sa(rctx, defaultaddr, sb);
			strbuf_printf(sb, "no suitable address for this category was found using (default)");
		}
	}
}

/** Initializer function (mandatory)
 *  Is called once the policy is loaded and every time it is reloaded
 *  Typically sets the policy_info and initializes the lists of candidate addresses
 */
int init(mam_context_t *mctx)
{
	printf("Policy module \"gpac_test\" is loading.\n");

	g_slist_foreach(mctx->prefixes, &set_policy_info, NULL);

	make_v4v6_enabled_lists (mctx->prefixes, &in4_enabled, &in6_enabled);

	printf("\nPolicy module \"gpac_test\" has been loaded.\n");
	return 0;
}

/** Cleanup function (mandatory)
 *  Is called once the policy is torn down, e.g. if MAM is terminates
 *  Tear down lists of candidate addresses (no deep free) and policy infos
 */
int cleanup(mam_context_t *mctx)
{
	g_slist_free(in4_enabled);
	g_slist_free(in6_enabled);
	g_slist_foreach(mctx->prefixes, &freepolicyinfo, NULL);

	printf("Policy \"gpac_test\" library cleaned up.\n");
	return 0;
}


/** Asynchronous callback function for socketconnect request after resolve
 *  Invoked once a response to the resolver query has been received
 *  Sends back a reply to the client with the received answer
 */
static void resolve_request_result_connect(int errcode, struct evutil_addrinfo *addr, void *ptr)
{
	strbuf_t sb;
	strbuf_init(&sb);
	intent_category_t category = -1;
	int filesize = -1;
	socklen_t cat_length = sizeof(intent_category_t);
	socklen_t filesize_length = sizeof(int);

	request_context_t *rctx = ptr;

	if (errcode) {
	    printf("\n\t Error resolving: %s -> %s\n", rctx->ctx->remote_hostname, evutil_gai_strerror(errcode));
	}
	else
	{
		printf("\n\t Got resolver response for %s: %s\n",
			rctx->ctx->remote_hostname,
			addr->ai_canonname ? addr->ai_canonname : "");

		assert(addr != NULL);
		assert(rctx->ctx->remote_addrinfo_res == NULL);
		rctx->ctx->remote_addrinfo_res = _muacc_clone_addrinfo(addr);
		print_addrinfo_response (rctx->ctx->remote_addrinfo_res);

		// Choose first result as the remote address
		rctx->ctx->domain = addr->ai_family;
		rctx->ctx->type = addr->ai_socktype;
		rctx->ctx->protocol = addr->ai_protocol;
		rctx->ctx->remote_sa_len = addr->ai_addrlen;
		rctx->ctx->remote_sa = _muacc_clone_sockaddr(addr->ai_addr, addr->ai_addrlen);

		// free libevent addrinfo
		evutil_freeaddrinfo(addr);

		// Find local address for destination
		strbuf_printf(&sb, "\tDestination address =");
		_muacc_print_sockaddr(&sb, rctx->ctx->remote_sa, rctx->ctx->remote_sa_len);
		strbuf_printf(&sb, "\n");

		struct socketopt *optlist = rctx->ctx->sockopts_current;

		if (0 != mampol_get_socketopt(optlist, SOL_INTENTS, INTENT_CATEGORY, &cat_length, &category))
        {
		// no category given
            strbuf_printf(&sb, "\n\tNo category intent given - checking for filesize rules.");
        }
        if (0 != mampol_get_socketopt(optlist, SOL_INTENTS, INTENT_FILESIZE, &filesize_length, &filesize))
        {
        // no filesize intents given
                strbuf_printf(&sb, "\n\t No filesize intent given. ");

        }

		else if(rctx->ctx->bind_sa_req != NULL)
		{	// already bound
			strbuf_printf(&sb, "\tAlready bound to src=");
			_muacc_print_sockaddr(&sb, rctx->ctx->bind_sa_req, rctx->ctx->bind_sa_req_len);
			strbuf_printf(&sb, "\n");
		}

			//strbuf_printf(&sb, "\t \n callin set sa for category ");
			set_sa(rctx, category, filesize, &sb);

			// search address to bind to
			if(rctx->ctx->bind_sa_suggested != NULL)
			{
				strbuf_printf(&sb, "\t \n Suggested source address for given intents, address: ");
				_muacc_print_sockaddr(&sb, rctx->ctx->bind_sa_suggested, rctx->ctx->bind_sa_suggested_len);
				strbuf_printf(&sb, "\n");
			}
			else
				strbuf_printf(&sb, "\tNo default interface is available!\n");

	}

	muacc_mam_action_t action = muacc_act_socketconnect_resp;
	// send response back
	if (rctx->action == muacc_act_socketchoose_req)
	{
		action = muacc_act_socketchoose_resp_new;
	}
	_muacc_send_ctx_event(rctx, action);

    printf("%s\n\n", strbuf_export(&sb));
    strbuf_release(&sb);
}

/** Socketconnect request function
 *  Is called upon each socketconnect request from a client
 *  Performs name resolution and then chooses a local address
 *  Must send a reply back using _muacc_sent_ctx_event or register a callback that does so
 */
int on_socketconnect_request(request_context_t *rctx, struct event_base *base)
{
    struct evdns_getaddrinfo_request *req;

	printf("\tSocketconnect request: %s:%s", (rctx->ctx->remote_hostname == NULL ? "" : rctx->ctx->remote_hostname), (rctx->ctx->remote_service == NULL ? "" : rctx->ctx->remote_service));

	/* Try to resolve this request using asynchronous lookup */
    req = evdns_getaddrinfo(
    		rctx->mctx->evdns_default_base,
			rctx->ctx->remote_hostname,
			rctx->ctx->remote_service,
            rctx->ctx->remote_addrinfo_hint,
			&resolve_request_result_connect,
			rctx);
	printf(" - Sending request to default nameserver\n");
    if (req == NULL) {
		/* returned immediately - Send reply to the client */
		_muacc_send_ctx_event(rctx, muacc_act_getaddrinfo_resolve_resp);
		printf("\tRequest failed.\n");
	}
	return 0;
}

/** Socketchoose request function
 *  Is called upon each socketchoose request from a client
 *  Chooses from a set of existing sockets
 *  Must send a reply back using _muacc_sent_ctx_event or register a callback that does so
 */
int on_socketchoose_request(request_context_t *rctx, struct event_base *base)
{
    struct evdns_getaddrinfo_request *req;

	printf("\tSocketchoose request: %s:%s", (rctx->ctx->remote_hostname == NULL ? "" : rctx->ctx->remote_hostname), (rctx->ctx->remote_service == NULL ? "" : rctx->ctx->remote_service));

	if (rctx->sockets != NULL)
	{
		printf("\tSuggest using socket %d\n", rctx->sockets->file);

		/* Provide the information to open a new similar socket, in case the suggested socket cannot be used */
		// i.e. copying the current ctxid into the cloned ctx
		uuid_t context_id;
		__uuid_copy(context_id, rctx->ctx->ctxid);
		rctx->ctx = _muacc_clone_ctx(rctx->sockets->ctx);
		__uuid_copy(rctx->ctx->ctxid, context_id);

		_muacc_send_ctx_event(rctx, muacc_act_socketchoose_resp_existing);
	}
	else
	{
		printf("\tSocketchoose with empty or almost empty set - trying to create new socket, resolving %s:%s\n", (rctx->ctx->remote_hostname == NULL ? "" : rctx->ctx->remote_hostname), (rctx->ctx->remote_service == NULL ? "" : rctx->ctx->remote_service));

		/* Try to resolve this request using asynchronous lookup */
		req = evdns_getaddrinfo(
    		rctx->mctx->evdns_default_base,
			rctx->ctx->remote_hostname,
			rctx->ctx->remote_service,
            rctx->ctx->remote_addrinfo_hint,
			&resolve_request_result_connect,
			rctx);
		printf(" - Sending request to default nameserver\n");
		if (req == NULL) {
			/* returned immediately - Send reply to the client */
			_muacc_send_ctx_event(rctx, muacc_act_getaddrinfo_resolve_resp);
			printf("\tRequest failed.\n");
		}
	}

	return 0;
}
