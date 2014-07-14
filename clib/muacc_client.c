#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include "uriparser/Uri.h"

#include "lib/dlog.h"

#include "lib/intents.h"

#include "muacc_client_util.h"

#ifndef CLIB_IF_NOISY_DEBUG0
#define CLIB_IF_NOISY_DEBUG0 1
#endif

#ifndef CLIB_IF_NOISY_DEBUG1
#define CLIB_IF_NOISY_DEBUG1 0
#endif

#ifndef CLIB_IF_NOISY_DEBUG2
#define CLIB_IF_NOISY_DEBUG2 0
#endif

int muacc_socket(muacc_context_t *ctx,
        int domain, int type, int protocol)
{
	int ret = -2;

	DLOG(CLIB_IF_NOISY_DEBUG2, "invoked\n");

	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular socket\n");
		goto muacc_socket_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			goto muacc_socket_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular socket\n");
		_unlock_ctx(ctx);
		goto muacc_socket_fallback;
	}

	ctx->ctx->calls_performed |= MUACC_SOCKET_CALLED;
	ctx->ctx->domain = domain;
	ctx->ctx->type = type;
	ctx->ctx->protocol = protocol;

	ret = socket(domain, type, protocol);
    
    ctx->ctx->ctxino = _muacc_get_ctxino(ret);

	_unlock_ctx(ctx);
	return ret;

	muacc_socket_fallback:

	return socket(domain, type, protocol);
}

int muacc_getaddrinfo(muacc_context_t *ctx,
		const char *hostname, const char *servname,
		const struct addrinfo *hints, struct addrinfo **res)
{

	int ret;

	DLOG(CLIB_IF_NOISY_DEBUG2, "invoked\n");

	/* check context and initialize if neccessary */
	if(ctx == NULL)
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular connect\n");
		goto muacc_getaddrinfo_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			goto muacc_getaddrinfo_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular connect\n");
		_unlock_ctx(ctx);
		goto muacc_getaddrinfo_fallback;
	}
	
	/* flag call performed */
	ctx->ctx->calls_performed |= MUACC_GETADDRINFO_CALLED;
	
	/* save hostname */
	if(ctx->ctx->remote_hostname != NULL)
		free(ctx->ctx->remote_hostname);
	ctx->ctx->remote_hostname = _muacc_clone_string(hostname);

	/* save hint */
	if(ctx->ctx->remote_addrinfo_hint != NULL)
		freeaddrinfo(ctx->ctx->remote_addrinfo_hint);
	ctx->ctx->remote_addrinfo_hint = _muacc_clone_addrinfo(hints);

	/* clear result from previous calls */
	if (ctx->ctx->remote_addrinfo_res != NULL)
	{
		ctx->ctx->remote_addrinfo_res = NULL;
	}

	DLOG(CLIB_IF_NOISY_DEBUG2, "contacting mam\n");

	/* contact mam */
	_muacc_contact_mam(muacc_act_getaddrinfo_resolve_req, ctx);

	if(ctx->ctx->remote_addrinfo_res != NULL)
	{
		DLOG(CLIB_IF_NOISY_DEBUG2, "using result from mam\n");
		
		*res = _muacc_clone_addrinfo(ctx->ctx->remote_addrinfo_res);
		ret = 0;
	}
	else
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "no result from mam - resolving name on my own\n");
		
		/* do query on our own */
		ret = 	 getaddrinfo(hostname, servname, hints, res);
		if (ret == 0)
		{
			/* save response */
			ctx->ctx->remote_addrinfo_res = _muacc_clone_addrinfo(*res);

		}
	}

	_unlock_ctx(ctx);

	return ret;

muacc_getaddrinfo_fallback:

	return getaddrinfo(hostname, servname, hints, res);

}


int muacc_setsockopt(muacc_context_t *ctx, int socket, int level, int option_name,
    const void *option_value, socklen_t option_len)
{
	int retval = -2; // Return value; will be set, else structure problem in function

	/* check context and initialize if neccessary */
	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular setsockopt\n");
		goto muacc_setsockopt_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			goto muacc_setsockopt_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular setsockopt\n");
		_unlock_ctx(ctx);
		goto muacc_setsockopt_fallback;
	}

	if (level == SOL_INTENTS)
	{
		// Intent socket options are handled by us
		if (option_value == NULL)
		{
			// Invalid buffer
			errno = EFAULT;
			_unlock_ctx(ctx);
			return -1;
		}
	}
	else
	{
		// Socket option not an intent: Call original setsockopt function
		if ((retval = setsockopt(socket, level, option_name, option_value, option_len)) < 0)
		{
			_unlock_ctx(ctx);
			return retval;
		}

		retval = 0;
	}

	/* we have set successfully an socket option or checked an intend - save for MAM */

	/* Go through sockopt list and look for this option */
	struct socketopt *current = ctx->ctx->sockopts_current;
	struct socketopt *prev = current;

	while (current != NULL && current->optname != option_name)
	{
		prev = current;
		current = current->next;
	}

	if (current != NULL)
	{
		/* Option already exists: overwrite value */
		memcpy(current->optval, option_value, current->optlen);
		DLOG(CLIB_IF_NOISY_DEBUG2, "Changed existing sockopt:\n\t\t\t");
		if (CLIB_IF_NOISY_DEBUG2) _muacc_print_socket_option_list(current);
	}
	else
	{
		/* Option did not exist: create new option in list */
		struct socketopt *newopt = malloc(sizeof(struct socketopt));
		newopt->level = level;
		newopt->optname = option_name;
		newopt->optlen = option_len;
		if(option_len > 0 && option_value != NULL)
		{
			newopt->optval = malloc(option_len);
			if (newopt->optval == NULL)
			{
				perror("__function__ malloc failed");
                free(newopt);
				_unlock_ctx(ctx);
				return retval;
			}
			memcpy(newopt->optval, option_value, option_len);
		}
		else
			newopt->optval = (void *) option_value;
		newopt->next = NULL;

		if (current == ctx->ctx->sockopts_current)
			ctx->ctx->sockopts_current = newopt;
		else
			prev->next = newopt;

		DLOG(CLIB_IF_NOISY_DEBUG2, "Added new option to the end of the list:\n\t\t\t");
		if (CLIB_IF_NOISY_DEBUG2) _muacc_print_socket_option_list(newopt);
		if (CLIB_IF_NOISY_DEBUG2) _muacc_print_socket_option_list(ctx->ctx->sockopts_current);
	}

	retval = 0;

	_unlock_ctx(ctx);

	return retval;

	muacc_setsockopt_fallback:

	return setsockopt(socket, level, option_name, option_value, option_len);

}

int muacc_getsockopt(muacc_context_t *ctx, int socket, int level, int option_name,
    void *option_value, socklen_t *option_len)
{
	int retval = -2; // Return value, will be set, else structure problem in function

	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular getsockopt\n");
		return getsockopt(socket, level, option_name, option_value, option_len);
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			return getsockopt(socket, level, option_name, option_value, option_len);
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular getsockopt\n");
		_unlock_ctx(ctx);
		return getsockopt(socket, level, option_name, option_value, option_len);
	}

	if( level == SOL_INTENTS)
	{
		// Intent socket options are handled by us
		if (option_value == NULL || option_len == NULL)
		{
			// Invalid buffer
			errno = EFAULT;
			_unlock_ctx(ctx);
			return -1;
		}

		DLOG(CLIB_IF_NOISY_DEBUG2, "Looking for socket option: \n\t\t\t{ { level = %d, optname = %d, value %p } }\n", level, option_name, option_value);

		struct socketopt *current = ctx->ctx->sockopts_current;
		while (current != NULL)
		{
			// Search for the option_name in this contexts' socket_option list
			if (current->optname == option_name)
			{
				// Found it!
				if ((memcpy(option_value, current->optval, current->optlen) == NULL) || (memcpy(option_len, &current->optlen, sizeof(size_t)) == NULL))
				{
					// Error copying data
					errno = EFAULT;
					retval = -1;
				}
				else
				{
					// Successfully copied data: End loop
					DLOG(CLIB_IF_NOISY_DEBUG2, "Found socket option: \n\t\t\t");
					if (CLIB_IF_NOISY_DEBUG2) _muacc_print_socket_option_list(current);

					retval = 0;
					break;
				}
			}
			current = current->next;
		}
		if (current == NULL)
		{
			// Reached end of list without finding the option
			errno = ENOPROTOOPT;
			retval = -1;
		}
	}
	else
	{
		// Requested socket option is not on 'intents' layer
		if ((retval = getsockopt(socket, level, option_name, option_value, option_len)) < 0)
		{
			_unlock_ctx(ctx);
			return retval;
		}
	}

	// If we arrive here, we have successfully gotten the option (intent or other)

	_unlock_ctx(ctx);

	return retval;
}

int muacc_bind(muacc_context_t *ctx, int socket, const struct sockaddr *address, socklen_t address_len)
{
	int ret = -1;

	DLOG(CLIB_IF_NOISY_DEBUG2, "invoked\n");

	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular connect\n");
		goto muacc_bind_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			goto muacc_bind_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular connect\n");
		_unlock_ctx(ctx);
		goto muacc_bind_fallback;
	}

	ctx->ctx->calls_performed |= MUACC_BIND_CALLED;
	ret = bind(socket, address, address_len);

	if (ret == 0)
	{
		ctx->ctx->bind_sa_req = _muacc_clone_sockaddr(address, address_len);
		ctx->ctx->bind_sa_req_len = address_len;
	}
	_unlock_ctx(ctx);
	return ret;

	muacc_bind_fallback:

	return bind(socket, address, address_len);

}

int muacc_connect(muacc_context_t *ctx,
	    int socket, const struct sockaddr *address, socklen_t address_len)
{
	struct socketopt *so = NULL;
	int retval;

	DLOG(CLIB_IF_NOISY_DEBUG2, "invoked\n");

	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular connect\n");
		goto muacc_connect_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized - trying to initialize\n");
		muacc_init_context(ctx);
		if( ctx->ctx == NULL )
			goto muacc_connect_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular connect\n");
		_unlock_ctx(ctx);
		goto muacc_connect_fallback;
	}

	ctx->ctx->calls_performed |= MUACC_CONNECT_CALLED;

	ctx->ctx->remote_sa     = _muacc_clone_sockaddr((struct sockaddr *)address, address_len);
	ctx->ctx->remote_sa_len = address_len;

	ctx->ctx->domain = address->sa_family;

	if( _muacc_contact_mam(muacc_act_connect_req, ctx) <0 ){
		_unlock_ctx(ctx);
		DLOG(CLIB_IF_NOISY_DEBUG0, "got no response from mam - fallback to regular connect\n");
		goto muacc_connect_fallback;
	}

	/* bind if no request but the mam suggestion exists */
	if ( ctx->ctx->bind_sa_req == NULL && ctx->ctx->bind_sa_suggested != NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "trying to bind with mam-supplied data\n");
		if( bind(socket, ctx->ctx->bind_sa_suggested, ctx->ctx->bind_sa_suggested_len) != 0 )
		{
			DLOG(CLIB_IF_NOISY_DEBUG0, "error binding with mam-supplied data: %s\n", strerror(errno));
		}
		else
		{
			DLOG(CLIB_IF_NOISY_DEBUG1, "binding with mam-supplied data succeeded\n");
		}
	}

	/* set socketopts */
	for(so = ctx->ctx->sockopts_suggested; so != NULL; so = so->next)
	{
		strbuf_t sb;
		strbuf_init(&sb);

		if (so->level == SOL_INTENTS)
		{
			/* skip option */
			DLOG(CLIB_IF_NOISY_DEBUG1, "skipping suggested SOL_INTENTS socketopt\n");
			continue;
		}

		#ifdef CLIB_IF_NOISY_DEBUG1
		strbuf_rewind(&sb); _muacc_print_socket_option(&sb, so);
		DLOG(CLIB_IF_NOISY_DEBUG1, "trying to setting suggested socketopt %s\n", strbuf_export(&sb));
		#endif

		if ( (retval = setsockopt(socket, so->level, so->optname, so->optval, so->optlen)) == -1 )
		{
			strbuf_rewind(&sb); _muacc_print_socket_option(&sb, so);
			DLOG(CLIB_IF_NOISY_DEBUG0, "setting suggested socketopt %s failed: %s\n", strbuf_export(&sb), strerror(errno));
		}
		strbuf_release(&sb);
	}

	/* unlock context and do request */
	_unlock_ctx(ctx);

	return connect(socket, ctx->ctx->remote_sa, ctx->ctx->remote_sa_len);


muacc_connect_fallback:

	return connect(socket, address, address_len);

}

int muacc_close(muacc_context_t *ctx,
        int socket)
{
	int ret = -2;
	ctx->ctx->calls_performed |= MUACC_CLOSE_CALLED;

	DLOG(CLIB_IF_NOISY_DEBUG2, "invoked\n");

	if( ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "NULL context - fallback to regular close\n");
		goto muacc_close_fallback;
	}
	else if( ctx->ctx == NULL )
	{
		DLOG(CLIB_IF_NOISY_DEBUG1, "context uninitialized\n");
		goto muacc_close_fallback;
	}

	if( _lock_ctx(ctx) )
	{
		DLOG(CLIB_IF_NOISY_DEBUG0, "WARNING: context already in use - fallback to regular close\n");
		_unlock_ctx(ctx);
		goto muacc_close_fallback;
	}

	ret = close(socket);

	/* Release and deinitialize context */
	if (0 == muacc_release_context(ctx))
		ctx->ctx = NULL;
	
	_unlock_ctx(ctx);

	return ret;

	muacc_close_fallback:

	return close(socket);
}

int socketconnect(int *s, const char *url, struct socketopt *sockopts, int domain, int type, int proto)
{
	DLOG(CLIB_IF_NOISY_DEBUG0, "Socketconnect request invoked\n");
	if (s == NULL)
		return -1;

	muacc_context_t ctx;
	muacc_init_context(&ctx);

	if (ctx.ctx == NULL)
	{
		return -1;
	}

	DLOG(CLIB_IF_NOISY_DEBUG2, "Context created\n");
	ctx.ctx->domain = domain;
	ctx.ctx->type = type;
	ctx.ctx->protocol = proto;
	ctx.ctx->sockopts_current = _muacc_clone_socketopts((const struct socketopt*) sockopts);

	if (*s == -1)
	{
		/* Socket does not exist yet */
		if (url != NULL)
		{
			ctx.ctx->remote_addrinfo_hint = malloc(sizeof(struct addrinfo));
			memset(ctx.ctx->remote_addrinfo_hint, 0, sizeof(struct addrinfo));
			ctx.ctx->remote_addrinfo_hint->ai_family = domain;
			ctx.ctx->remote_addrinfo_hint->ai_socktype = type;
			ctx.ctx->remote_addrinfo_hint->ai_protocol = proto;

			DLOG(CLIB_IF_NOISY_DEBUG2, "Creating a new socket to connect to %s\n", url);
			UriParserStateA state;
			UriUriA uri;

			state.uri = &uri;
			if (uriParseUriA(&state, url) != URI_SUCCESS)
			{
				/* Failed to parse URL */
				DLOG(CLIB_IF_NOISY_DEBUG1, "Failed to parse URL\n");
				uriFreeUriMembersA(&uri);
				muacc_release_context(&ctx);
				return -1;
			}

			int hostnamelen = uri.hostText.afterLast - uri.hostText.first;

			ctx.ctx->remote_hostname = malloc(hostnamelen + 1);
			ctx.ctx->remote_hostname[hostnamelen] = 0;
			ctx.ctx->remote_hostname = strncpy(ctx.ctx->remote_hostname, uri.hostText.first, hostnamelen);
			uri.portText.afterLast = 0;
			ctx.ctx->remote_port = atoi(uri.portText.first);

			uriFreeUriMembersA(&uri);

			if (CLIB_IF_NOISY_DEBUG2)
			{
				printf("Before MAM:\n");
				muacc_print_context(&ctx);
			}

			if (-1 == _muacc_contact_mam(muacc_act_socketconnect_req, &ctx))
			{
				DLOG(CLIB_IF_NOISY_DEBUG1, "Got no response from MAM (Is it running?) - Failing.\n");
				muacc_release_context(&ctx);
				return -1;
			}

			if (CLIB_IF_NOISY_DEBUG2)
			{
				printf("After MAM:\n");
				muacc_print_context(&ctx);
			}

			DLOG(CLIB_IF_NOISY_DEBUG2, "Got response from MAM - Creating socket now (Domain: %d, Type: %d, Protocol: %d)\n", ctx.ctx->domain, ctx.ctx->type, ctx.ctx->protocol);
			if ((*s = socket(ctx.ctx->domain, ctx.ctx->type, ctx.ctx->protocol)) != -1)
			{
				DLOG(CLIB_IF_NOISY_DEBUG2, "Successfully created socket %d\n", *s);
			}
			else
			{
				DLOG(CLIB_IF_NOISY_DEBUG2, "Failed to create socket: %s\n", strerror(errno));				
			}

			DLOG(CLIB_IF_NOISY_DEBUG2, "Setting suggested socket options\n");
			if (CLIB_IF_NOISY_DEBUG2)
			{
				printf("Socket options:\n");
				_muacc_print_socket_option_list(ctx.ctx->sockopts_suggested);
			}

			struct socketopt *so = NULL;
			for (so = ctx.ctx->sockopts_suggested; so != NULL; so = so->next)
			{
				so->returnvalue = muacc_setsockopt(&ctx, *s, so->level, so->optname, so->optval, so->optlen);
				if (so->returnvalue == -1)
				{
					DLOG(CLIB_IF_NOISY_DEBUG1, "Setting sockopt failed: %s\n", strerror(errno));
					if (so->flags && SOCKOPT_OPTIONAL != 0)
					{
						// fail
						DLOG(CLIB_IF_NOISY_DEBUG2, "Socket option was mandatory, but failed - returning\n");
						muacc_release_context(&ctx);
						return -1;
					}
				}
				else
				{
					DLOG(CLIB_IF_NOISY_DEBUG2, "Socket option was set successfully\n");
					so->flags &= SOCKOPT_IS_SET;
				}

			}

			if (ctx.ctx->bind_sa_suggested != NULL)
			{
				DLOG(CLIB_IF_NOISY_DEBUG2, "Attempting to bind socket %d\n", *s);
				if (CLIB_IF_NOISY_DEBUG2)
				{
					printf("Local address:\n");
					_muacc_print_socket_addr(ctx.ctx->bind_sa_suggested, ctx.ctx->bind_sa_suggested_len);
					printf("\n");
				}
	
				if (0 == bind(*s, ctx.ctx->bind_sa_suggested, ctx.ctx->bind_sa_suggested_len))
				{
					DLOG(CLIB_IF_NOISY_DEBUG2, "Bound socket to suggested local address\n");
				}
				else
				{
					DLOG(CLIB_IF_NOISY_DEBUG1, "Error binding to local address: %s\n", strerror(errno));
				}
			}

			if (ctx.ctx->remote_sa != NULL)
			{
				if (ctx.ctx->domain == AF_INET) 
					((struct sockaddr_in *) ctx.ctx->remote_sa)->sin_port = htons(ctx.ctx->remote_port);
		        else if (ctx.ctx->domain == AF_INET6)
		            ((struct sockaddr_in6 *) ctx.ctx->remote_sa)->sin6_port = htons(ctx.ctx->remote_port);

				DLOG(CLIB_IF_NOISY_DEBUG2, "Attempting to connect the socket\n");
				if (CLIB_IF_NOISY_DEBUG2)
				{
					printf("Remote address:\n");
					_muacc_print_socket_addr(ctx.ctx->remote_sa, ctx.ctx->remote_sa_len);
					printf("\n");
				}
		
			if (0 == connect(*s, ctx.ctx->remote_sa, ctx.ctx->remote_sa_len))
				{
					DLOG(CLIB_IF_NOISY_DEBUG2, "Socket was successfully connected - returning\n");
				}
				else
				{
					DLOG(CLIB_IF_NOISY_DEBUG1, "Connection failed: %s\n", strerror(errno));
				}
			}
			else
			{
				DLOG(CLIB_IF_NOISY_DEBUG1, "Got no remote address to connect to - fail\n");
				muacc_release_context(&ctx);
				return -1;
			}
			muacc_release_context(&ctx);
			return 1;
		}
		else
		{
			muacc_release_context(&ctx);
			return -1;
		}
	}
	else
	{
		/* Socket exists - TODO: Search for corresponding socket set and send socketchoose to MAM */
		muacc_release_context(&ctx);
		return 0;
	}
}
