#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netdb.h>
#include <errno.h>

#include "uriparser/Uri.h"

#include "dlog.h"
#include "lib/muacc_ctx.h"
#include "lib/muacc_tlv.h"
#include "lib/intents.h"

#include "muacc_client_util.h"
#include "config.h"

#ifndef MUACC_CLIENT_UTIL_NOISY_DEBUG0
#define MUACC_CLIENT_UTIL_NOISY_DEBUG0 0
#endif

#ifndef MUACC_CLIENT_UTIL_NOISY_DEBUG1
#define MUACC_CLIENT_UTIL_NOISY_DEBUG1 0
#endif

#ifndef MUACC_CLIENT_UTIL_NOISY_DEBUG2
#define MUACC_CLIENT_UTIL_NOISY_DEBUG2 0
#endif

int muacc_init_context(struct muacc_context *ctx)
{
	struct _muacc_ctx *_ctx = _muacc_create_ctx();

	if(_ctx == NULL || ctx == NULL)
		return(-1);
	_ctx->ctxid = _get_ctxid();

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1,"context successfully initialized\n");

	ctx->usage = 1;
	ctx->locks = 0;
	ctx->mamsock = -1;

	ctx->ctx = _ctx;
	return(0);
}

muacc_ctxino_t _muacc_get_ctxino(int sockfd)
{
    struct stat s;
    fstat(sockfd, &s);
    return s.st_ino;
}

int _lock_ctx (muacc_context_t *ctx)
{
    return( -(ctx->locks++) );
}

int _unlock_ctx (muacc_context_t *ctx)
{
    return( -(--(ctx->locks)) );
}

int muacc_retain_context(struct muacc_context *ctx)
{
	if(ctx->ctx == NULL)
	{
		return(-1);
	}

	return(++(ctx->usage));
}

void muacc_print_context(struct muacc_context *ctx)
{
	strbuf_t sb;
	
	if (ctx == NULL)
	{
		printf("ctx = NULL\n");
	}
	else if (ctx->ctx == NULL)
	{
		printf("ctx->ctx = NULL\n");
	}
	else
	{
		strbuf_init(&sb);
		_muacc_print_ctx(&sb, ctx->ctx);
		printf("/**************************************/\n%s\n/**************************************/\n", strbuf_export(&sb));
		strbuf_release(&sb);
	}
}

int muacc_release_context(struct muacc_context *ctx)
{
	if(ctx == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: tried to release NULL POINTER context\n");
		return -1;
	}
	else if(ctx->ctx == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "empty context - nothing to release\n");
		return 0;
	}
	else
	{
		if( --(ctx->usage) == 0 )
		{
			if (ctx->mamsock != -1) 				close(ctx->mamsock);
			return _muacc_free_ctx(ctx->ctx);
		} else {
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "context has still %d references\n", ctx->usage);
			return(ctx->usage);
		}
	}
}


int muacc_clone_context(struct muacc_context *dst, struct muacc_context *src)
{
	if (dst == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0,"WARNING: cloning into empty context\n");
		return 0;
	}

	if (src == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0,"WARNING: cloning uninitialized context\n");
		dst->ctx = NULL;
	}
	else
	{
		dst->ctx = _muacc_clone_ctx(src->ctx);
	}

	dst->usage = 1;
	dst->locks = 0;
	dst->mamsock = -1;

	return(0);
}



int _muacc_connect_ctx_to_mam(muacc_context_t *ctx)
{
	struct sockaddr_un mams;
	mams.sun_family = AF_UNIX;
	#ifdef HAVE_SOCKADDR_LEN
	mams.sun_len = sizeof(struct sockaddr_un);
	#endif

	if(	ctx->mamsock != -1 )
		return 0;

	strncpy( mams.sun_path, MUACC_SOCKET, sizeof(mams.sun_path));
	ctx->mamsock = socket(PF_UNIX, SOCK_STREAM, 0);
	if(ctx->mamsock == -1)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: socket creation failed: %s\n", strerror(errno));
		return(-errno);
	}

	if(connect(ctx->mamsock, (struct sockaddr*) &mams, sizeof(mams)) < 0)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: connect to mam via %s failed: %s\n",  mams.sun_path, strerror(errno));
		close(ctx->mamsock);
		ctx->mamsock = -1;
		return(-errno);
	}

	return 0;
}


int _muacc_contact_mam (muacc_mam_action_t reason, muacc_context_t *ctx)
{

	char buf[MUACC_TLV_MAXLEN];
	ssize_t pos = 0;
	ssize_t ret = 0;
	muacc_tlv_t tag;
	void *data;
	ssize_t data_len;

	/* connect to MAM */
	if(	_muacc_connect_ctx_to_mam(ctx) != 0 )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: failed to contact MAM\n");
		goto _muacc_contact_mam_connect_err;
	}

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "packing request\n");

	/* pack request */
	if( 0 > _muacc_push_tlv(buf, &pos, sizeof(buf), action, &reason, sizeof(muacc_mam_action_t)) ) goto  _muacc_contact_mam_pack_err;
	if( 0 > _muacc_pack_ctx(buf, &pos, sizeof(buf), ctx->ctx) ) goto  _muacc_contact_mam_pack_err;
	if( 0 > _muacc_push_tlv_tag(buf, &pos, sizeof(buf), eof) ) goto  _muacc_contact_mam_pack_err;
	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2,"packing request done\n");


	/* send requst */
	if( 0 > (ret = send(ctx->mamsock, buf, pos, 0)) )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: error sending request: %s\n", strerror(errno));
		goto _muacc_contact_mam_connect_err;
	}
	else
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "request sent  - %ld of %ld bytes\n", (long int) ret, (long int) pos);
	}

	/* read & unpack response */
	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "processing response:\n");
	pos = 0;
	while( (ret = _muacc_read_tlv(ctx->mamsock, buf, &pos, sizeof(buf), &tag, &data, &data_len)) > 0)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "\tpos=%ld tag=%x, len=%ld\n", (long int) pos, tag, (long int) data_len);
		if( tag == eof )
			break;
		else if ( 0 > _muacc_unpack_ctx(tag, data, data_len, ctx->ctx) )
			goto  _muacc_contact_mam_parse_err;
	}
	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "processing response done: pos=%li last_res=%li done\n", (long int) pos, (long int) ret);
	return(0);

_muacc_contact_mam_connect_err:
	return(-1);

_muacc_contact_mam_pack_err:

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: failed to pack request\n");
	return(-1);

_muacc_contact_mam_parse_err:

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: failed to process response\n");
	return(-1);

}

int muacc_set_intent(socketopt_t **opts, int optname, const void *optval, socklen_t optlen, int flags)
{
	return _muacc_add_sockopt_to_list(opts, SOL_INTENTS, optname, optval, optlen, flags);
}

int muacc_free_socket_option_list(struct socketopt *opts)
{
	if (opts != 0)
	{
		_muacc_free_socketopts(opts);
		return 0;
	}

	return -1;
}

int _muacc_add_socket_to_list(struct socketlist **list, int socket, struct _muacc_ctx *ctx)
{
	struct socketset *set = NULL;
	struct socketlist *newlist = NULL;
	if ((set = _muacc_find_set_for_socket(*list, ctx)) == NULL)
	{
		/* No matching socket set - create it */
		newlist = malloc(sizeof(struct socketlist));
		newlist->next = NULL;
		newlist->set = malloc(sizeof(struct socketset));
		newlist->set->next = NULL;
		newlist->set->file = socket;
		newlist->set->ctx = _muacc_clone_ctx(ctx);

		if (*list == NULL)
		{
			*list = newlist;
		}
		else
		{
			while ((*list)->next !=NULL)
			{
				*list = (*list)->next;
			}
			(*list)->next = newlist;
		}
	}
	else
	{
		/* Add socket to existing socket set */
		while (set->next != NULL)
		{
			set = set->next;
		}
		set->next = malloc(sizeof(struct socketset));
		set->next->next = NULL;
		set->next->file = socket;
		set->next->ctx = _muacc_clone_ctx(ctx);
	}
	return 0;
}

struct socketset *_muacc_find_set_for_socket(struct socketlist *list, struct _muacc_ctx *ctx)
{
	while (list != NULL && list->set != NULL)
	{
		if (list->set->ctx->domain == ctx->domain && list->set->ctx->type == ctx->type && list->set->ctx->protocol == ctx->protocol && (memcmp(list->set->ctx->remote_sa, ctx->remote_sa, (ctx->domain == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == 0))
		{
			return list->set;
		}

		list = list->next;
	}

	return NULL;
}

struct socketset *_muacc_find_socketset(struct socketlist *list, int socket)
{
	while (list != NULL )
	{
		struct socketset *set = _muacc_socketset_find_file (list->set, socket);
		if (set != NULL)
			return set;
		list = list->next;
	}
	return NULL;
}

void muacc_print_socketlist(struct socketlist *list)
{
	printf("\n\t\tSocketlist:\n{ ");
	while (list != NULL)
	{
		printf("{ ");
		struct socketset *set = list->set;
		while (set != NULL)
		{
			strbuf_t sb;
			strbuf_init(&sb);

			printf("{ file = %d\n", set->file);
			printf("ctx = ");
			_muacc_print_ctx(&sb, set->ctx);
			printf("%s", strbuf_export(&sb));
			strbuf_release(&sb);

			set = set->next;
			printf("}, ");
		}
		printf("} <next socket set...>\n");
		list = list->next;
	}
	printf("}\n\n");
}

int _muacc_send_socketchoose (muacc_context_t *ctx, int *socket, struct socketset *set)
{
	int returnvalue = -1;

	char buf[MUACC_TLV_MAXLEN];
	ssize_t pos = 0;
	ssize_t ret = 0;
	muacc_tlv_t tag;
    void *data;
    ssize_t data_len;

	muacc_mam_action_t reason = muacc_act_socketchoose_req;

	if ( _muacc_connect_ctx_to_mam(ctx) != 0 )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "WARNING: failed to contact MAM\n");
		return -1;
	}

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "packing request\n");
	if ( 0 > _muacc_push_tlv(buf, &pos, sizeof(buf), action, &reason, sizeof(muacc_mam_action_t)) )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error packing label\n");
		return -1;
	}

	/* Pack context from request */
	if( 0 > _muacc_pack_ctx(buf, &pos, sizeof(buf), ctx->ctx) )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error packing request context of %d\n", set->file);
		return -1;
	}

	/* Pack contexts from socketset */
	while (set != NULL)
	{
		if ( 0 > _muacc_push_tlv(buf, &pos, sizeof(buf), socketset_file, &(set->file), sizeof(int)) )
		{
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error packing socketset file descriptor %d\n", set->file);
			return -1;
		}
		if( 0 > _muacc_pack_ctx(buf, &pos, sizeof(buf), set->ctx) )
		{
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error packing socket set context of %d\n", set->file);
			return -1;
		}
		set = set->next;
	}
	if( 0 > _muacc_push_tlv_tag(buf, &pos, sizeof(buf), eof) )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error packing eof\n");
		return -1;
	}
	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "packing request done\n");

	if ( 0 > (ret = send(ctx->mamsock, buf, pos, 0)) )
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error sending request: %s\n", strerror(errno));
		return -1;
	}
	else
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Sent request - %ld of %ld bytes\n", (long int) ret, (long int) pos);
	}

	/* read & unpack response */
    DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Getting response:\n");
    pos = 0;
	int ret2 = -1;

    while( (ret = _muacc_read_tlv(ctx->mamsock, buf, &pos, sizeof(buf), &tag, &data, &data_len)) > 0)
    {
        DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "\tpos=%ld tag=%x, len=%ld\n", (long int) pos, tag, (long int) data_len);
		if (tag == action)
		{
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Got action tag, data= %d\n", *(muacc_mam_action_t *) data);
			if (*(muacc_mam_action_t *) data == muacc_act_socketchoose_resp_existing)
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "MAM says: Use existing socket!\n");
			}
			else if (*(muacc_mam_action_t *) data == muacc_act_socketchoose_resp_new)
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "MAM says: Open a new socket!\n");
				*socket = -1;
				returnvalue = 1;
			}
			else if (*(muacc_mam_action_t *) data == muacc_error_unknown_request)
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error: MAM sent error code \"Unknown Request\" -- Aborting.\n");
				return -1;
			}
			else
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error: Unknown MAM Response Action Type\n");
				return -1;
			}
		}
		else if (tag == socketset_file && data_len == sizeof(int))
		{
			memcpy(socket, (int *)data, data_len);
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG0, "Use socket %d from set.\n", *socket);
			returnvalue = 0;
		}
        else if( tag == eof )
            break;
        else
		{
			ret2 = _muacc_unpack_ctx(tag, data, data_len, ctx->ctx);
			if ( 0 > ret2 )
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Error unpacking context\n");
				return -1;
			}
		}
    }
    DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "processing response done: pos=%li last_res=%li done\n", (long int) pos, (long int) ret);

	return returnvalue;
}

struct socketset *_muacc_socketset_find_file (struct socketset *set, int socket)
{
	while (set != NULL)
	{
		if (set->file == socket)
			return set;
		set = set->next;
	}
	return NULL;
}

struct socketset *_muacc_socketset_find_dup (struct socketset *set)
{
	struct socketset *duplicate = set->next;

	while (duplicate != NULL)
	{
		if (duplicate->ctx == set->ctx)
		{
			// Found duplicate! (i.e. different file descriptor, but same socket context)
			break;
		}
		duplicate = duplicate->next;
	}

	return duplicate;
}

int _muacc_remove_socket_from_list (struct socketlist **list, int socket)
{
	struct socketlist *currentlist = *list;
	struct socketset *currentset = NULL;

	struct socketlist *base = NULL;
	struct socketlist *prevlist = NULL;
	struct socketset *set = NULL;
	struct socketset *prevset = NULL;

	DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Trying to delete set for socket %d\n", socket);

	while (currentlist != NULL)
	{
		// Go through list of socket sets
		currentset = currentlist->set;

		if (currentset->next == NULL)
		{
			// This set contains only one socket
			if (currentset->file == socket)
			{
				DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Found set to delete for %d\n", socket);
				set = currentset; // Found the set to delete!
				base = currentlist; // Store list entry of set
				prevset = NULL;
				break;
			}
		}
		else
		{
			// This set contains more than one socket
			while (currentset->next != NULL)
			{
				if (currentset->next->file == socket)
				{
					DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Found set to delete for %d\n", socket);
					set = currentset->next; // Found the set to delete!
					base = currentlist; // Store list entry of set
					prevset = currentset;
					break;
				}

				currentset = currentset->next;
			}
		}
		prevlist = currentlist;
		base = base->next;
	}

	if (set == NULL || base == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Socket %d not found in set!\n", socket);
		return -1;
	}
	else
	{
		// Free context if no other file descriptor needs it
		if (_muacc_socketset_find_dup(set) == NULL)
		{
			// No duplicate (i.e., no other file descriptor shares this socket/context)
			_muacc_free_ctx(set->ctx);
		}

		// Re-adjust pointers
		if (prevset != NULL)
		{
			prevset->next = set->next;
		}

		// Remove socket set entry
		free(set);

		if (prevset == NULL)
		{
			// This was the only socket in the set - clear the list entry
			if (prevlist != NULL)
			{
				// Set the pointer of the previous list entry
				prevlist->next = base->next;
			}
			else
			{
				// We freed the first list entry - set the list head
				*list = base->next;
			}
			free(base);
		}
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Socketset successfully cleared\n");

	}
	return 0;
}

int _muacc_parse_url_to_ctx(muacc_context_t *ctx, const char *url)
{
	if (ctx == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "No context given - aborting.\n");
		return -1;
	}
	else if (url == NULL)
	{
		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "No url given - cannot parse.\n");
		return -1;
	}
	else
	{
		ctx->ctx->remote_addrinfo_hint = malloc(sizeof(struct addrinfo));
		memset(ctx->ctx->remote_addrinfo_hint, 0, sizeof(struct addrinfo));
		ctx->ctx->remote_addrinfo_hint->ai_family = ctx->ctx->domain;
		ctx->ctx->remote_addrinfo_hint->ai_socktype = ctx->ctx->type;
		ctx->ctx->remote_addrinfo_hint->ai_protocol = ctx->ctx->protocol;

		DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG2, "Parsing URL %s into context\n", url);
		UriParserStateA state;
		UriUriA uri;

		state.uri = &uri;
		if ((uriParseUriA(&state, url) != URI_SUCCESS) || (uri.hostText.first == NULL || uri.portText.first == NULL))
		{
			/* Failed to parse URL */
			DLOG(MUACC_CLIENT_UTIL_NOISY_DEBUG1, "Failed to parse URL: %s (Does it contain a protocol, hostname, and port?)\n", url);
			uriFreeUriMembersA(&uri);
			return -1;
		}

		int hostnamelen = uri.hostText.afterLast - uri.hostText.first;

		ctx->ctx->remote_hostname = malloc(hostnamelen + 1);
		ctx->ctx->remote_hostname[hostnamelen] = 0;
		ctx->ctx->remote_hostname = strncpy(ctx->ctx->remote_hostname, uri.hostText.first, hostnamelen);
		uri.portText.afterLast = 0;
		ctx->ctx->remote_port = atoi(uri.portText.first);

		uriFreeUriMembersA(&uri);
		return 0;
	}
}
