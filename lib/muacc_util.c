#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

#include "../config.h"

#include "muacc.h"
#include "muacc_util.h"

#ifdef USE_SO_INTENTS
#include "intents.h"
#endif

#ifndef MUACC_UTIL_NOISY_DEBUG
#define MUACC_UTIL_NOISY_DEBUG 0
#endif


struct sockaddr *_muacc_clone_sockaddr(const struct sockaddr *src, size_t src_len)
{
	struct sockaddr *ret = NULL;

	if(src == NULL)
		return(NULL);

	if((ret = malloc(src_len)) == NULL)
		return NULL;

	memcpy(ret, src, src_len);

	return(ret);
}


char *_muacc_clone_string(const char *src)
{
	char* ret = NULL;

	if ( src != NULL)
	{
		size_t sl = strlen(src)+1;
		if( ( ret = malloc(sl) ) == NULL )
			return(NULL);
		memcpy( ret, src, sl);
		ret[sl] = 0x00;
	}

	return(ret);
}


struct addrinfo *_muacc_clone_addrinfo(const struct addrinfo *src)
{
	struct addrinfo *res = NULL;
	struct addrinfo **cur = &res;

    const struct addrinfo *ai;

	if(src == NULL)
		return(NULL);

	for (ai = src; ai; ai = ai->ai_next)
	{
		/* allocate memory and copy */
		if( (*cur = malloc(sizeof(struct addrinfo))) == NULL )
			goto _muacc_clone_addrinfo_malloc_err;
		memcpy( *cur, ai, sizeof(struct addrinfo));

		if ( ai->ai_addr != NULL)
		{
			(*cur)->ai_addr = _muacc_clone_sockaddr(ai->ai_addr, ai->ai_addrlen);
			if((*cur)->ai_addr == NULL)
				goto _muacc_clone_addrinfo_malloc_err;
		}

		if ( ai->ai_canonname != NULL)
		{
			if( ( (*cur)->ai_canonname = _muacc_clone_string(ai->ai_canonname)) == NULL )
				goto _muacc_clone_addrinfo_malloc_err;
		}

		cur = &((*cur)->ai_next);

	}

	return res;

	_muacc_clone_addrinfo_malloc_err:
	fprintf(stderr, "%6d: _muacc_clone_addrinfo failed to allocate memory\n", (int) getpid());
	return NULL;

}


struct socketopt *_muacc_clone_socketopts(const struct socketopt *src)
{
	struct socketopt *ret = NULL;

	if (src == NULL)
		return NULL;

	if ((ret = malloc(sizeof(struct socketopt))) == NULL)
	{
		fprintf(stderr, "%6d: _muacc_clone_socketopts failed to allocate memory.\n", (int) getpid());
		return NULL;
	}
	else
	{
		const struct socketopt *srccurrent = src;
		struct socketopt *dstcurrent = ret;
		memcpy(ret, src, sizeof(struct socketopt));
		if (src->optlen > 0 && src->optval != NULL)
		{
			if ((ret->optval = malloc(src->optlen)) == NULL)
			{
				fprintf(stderr, "%6d: _muacc_clone_socketopts failed to allocate memory.\n", (int) getpid());
				return NULL;
			}
			memcpy(ret->optval, src->optval, src->optlen);
		}

		while (srccurrent->next != NULL)
		{
			struct socketopt *new = NULL;
			if ((new = malloc(sizeof(struct socketopt))) == NULL)
			{
				fprintf(stderr, "%6d: _muacc_clone_socketopts failed to allocate memory.\n", (int) getpid());
				return NULL;
			}


			dstcurrent-> next = new;
			memcpy(new, srccurrent->next, sizeof(struct socketopt));

			if(srccurrent->next->optlen > 0 && srccurrent->next->optval != NULL)
			{
				if ((new->optval = malloc(new->optlen)) == NULL)
				{
					fprintf(stderr, "%6d: _muacc_clone_socketopts failed to allocate memory.\n", (int) getpid());
					return NULL;
				}
				memcpy(new->optval, srccurrent->next->optval, srccurrent->next->optlen);
			}

			srccurrent = srccurrent->next;
			dstcurrent = dstcurrent->next;
		}
	}

	return ret;
}

void _muacc_free_socketopts(struct socketopt *so)
{
	struct socketopt *next = so;

	while(next != NULL)
	{
		struct socketopt *last = next;
		next = last->next;

		if(last->optlen > 0 && last->optval != NULL)
			free(last->optval);

		free(last);
	}

}



void _muacc_print_sockaddr(strbuf_t *sb, const struct sockaddr *addr, size_t src_len)
{
	strbuf_printf(sb, "{ ");
	if (addr == NULL)
		strbuf_printf(sb, "NULL");
	else
	{
		if (addr->sa_family == AF_INET)
		{
			struct sockaddr_in *inaddr = (struct sockaddr_in *) addr;
			strbuf_printf(sb, "sin_family = AF_INET, ");
			strbuf_printf(sb, "sin_port = %d, ", ntohs(inaddr->sin_port));
			char ipaddr[INET_ADDRSTRLEN];
			if (inet_ntop(AF_INET, &inaddr->sin_addr, ipaddr, INET_ADDRSTRLEN) != NULL)
				strbuf_printf(sb, "sin_addr = %s", ipaddr);
		}
		else if (addr->sa_family == AF_INET6)
		{
			struct sockaddr_in6 *inaddr = (struct sockaddr_in6 *) addr;
			strbuf_printf(sb, "sin6_family = AF_INET6, ");
			strbuf_printf(sb, "sin6_port = %d, ", ntohs(inaddr->sin6_port));
			strbuf_printf(sb, "sin6_flowinfo = %d, ", inaddr->sin6_flowinfo);
			char ipaddr[INET6_ADDRSTRLEN];
			if (inet_ntop(AF_INET6, &inaddr->sin6_addr, ipaddr, INET6_ADDRSTRLEN) != NULL)
				strbuf_printf(sb, "sin6_addr = %s, ", ipaddr);
			strbuf_printf(sb, "sin6_scope_id = %d", inaddr->sin6_scope_id);
		}
		else if (addr->sa_family == AF_UNIX)
		{
			struct sockaddr_un *unaddr = (struct sockaddr_un *) addr;
			strbuf_printf(sb, "sun_family = AF_UNIX, ");
			strbuf_printf(sb, "sun_path = %s",unaddr->sun_path);
		}
		else
		{
			strbuf_printf(sb, "sa_family = %d <unknown>", addr->sa_family);
		}
	}
	strbuf_printf(sb, " }");
}

void _muacc_print_addrinfo(strbuf_t *sb, const struct addrinfo *addr)
{
	const struct addrinfo *current = addr;

	strbuf_printf(sb, "{ ");

	while (current != NULL)
	{
		strbuf_printf(sb, "{ ");
		strbuf_printf(sb, "ai_flags = %d, ", current->ai_flags);
		strbuf_printf(sb, "ai_family = %d, ", current->ai_family);
		strbuf_printf(sb, "ai_socktype = %d, ", current->ai_socktype);
		strbuf_printf(sb, "ai_protocol = %d, ", current->ai_protocol);
		strbuf_printf(sb, "ai_addr = ");
		_muacc_print_sockaddr( sb, current->ai_addr, current->ai_addrlen);
		strbuf_printf(sb, ", ");
		strbuf_printf(sb, "ai_canonname = %s", current->ai_canonname);
		strbuf_printf(sb, " }, ");
		current = current->ai_next;
	}

	strbuf_printf(sb, "NULL }");

}

char *_muacc_get_socket_level (int level)
{
	struct protoent *p;

	switch(level)
	{
		case SOL_SOCKET:
			return "SOL_SOCKET";
		#ifdef USE_SO_INTENTS
		case SOL_INTENTS:
			return "SOL_INTENTS";
		#endif
		default:
			p = getprotobynumber(level);
			if(p == NULL)
				return "SOL_UNKNOWN";
			else
				return p->p_name;
	}
}

void _muacc_print_socket_option_list(const struct socketopt *opts)
{
	strbuf_t sb;

	strbuf_init(&sb);
	_muacc_print_socket_options(&sb, opts);
	printf("%s\n", strbuf_export(&sb));
	strbuf_release(&sb);
}

void _muacc_print_socket_option(strbuf_t *sb, const struct socketopt *current)
{
	strbuf_printf(sb, "{ ");
	strbuf_printf(sb, "level = %d (%s), ", current->level, _muacc_get_socket_level(current->level));
	strbuf_printf(sb, "optname = %d, ", current->optname);
	if (current-> optval == NULL)
	{
		strbuf_printf(sb, "optval = NULL, ");
	}
	else
	{
		int *value = current->optval;
		strbuf_printf(sb, "optval = %d, ", *value);
	}
	strbuf_printf(sb, "optlen = %d ", current->optlen);
	strbuf_printf(sb, " }");
}

void _muacc_print_socket_options(strbuf_t *sb, const struct socketopt *opts)
{
	strbuf_printf(sb, "{ ");
	if (opts == NULL)
		strbuf_printf(sb, "NULL");
	else
	{
		const struct socketopt *current = opts;
		while (current != NULL)
		{
			_muacc_print_socket_option(sb, current);
			current = current->next;
		}
	}
	strbuf_printf(sb, " }");
}
