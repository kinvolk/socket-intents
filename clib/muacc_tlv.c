#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <assert.h>


#include "../config.h"

#include "muacc.h"
#include "muacc_ctx.h"
#include "muacc_tlv.h"
#include "muacc_util.h"
#include "dlog.h"

#ifndef CLIB_TLV_NOISY_DEBUG0
#define CLIB_TLV_NOISY_DEBUG0 1
#endif

#ifndef CLIB_TLV_NOISY_DEBUG1
#define CLIB_TLV_NOISY_DEBUG1 0
#endif

#ifndef CLIB_TLV_NOISY_DEBUG2
#define CLIB_TLV_NOISY_DEBUG2 0
#endif


size_t _muacc_push_tlv_tag( char *buf, size_t *buf_pos, size_t buf_len,
	muacc_tlv_t tag)
{
	return _muacc_push_tlv(buf, buf_pos, buf_len, tag, NULL, 0);

}

size_t _muacc_push_tlv( char *buf, size_t *buf_pos, size_t buf_len,
	muacc_tlv_t tag, 
	const void *data, size_t data_len)
{
	size_t tlv_len = sizeof(muacc_tlv_t)+sizeof(size_t)+data_len;
	
	/* check size */
	if (buf == NULL)
	{
		/* checking case */
		DLOG(CLIB_TLV_NOISY_DEBUG1, "length checking done - total length is %ld - returning\n", (long) data_len);
		return(tlv_len);
	}
	else if ( *buf_pos + tlv_len >= buf_len)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: buffer too small: buf_len=%li, pos=%li needed=%li\n", (long) buf_len, (long) *buf_pos, (long) tlv_len);
		return(-1);
	}
	
	*((muacc_tlv_t *) (buf + *buf_pos)) = tag;
	*buf_pos += sizeof(muacc_tlv_t);
	
	*((size_t *) (buf + *buf_pos)) = data_len;
	*buf_pos += sizeof(size_t);

	if(data == NULL && data_len != 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: trying to push NULL to a non zero length TLV\n");
	}

	if(data_len != 0)
	{
		memcpy( (void *) (buf + *buf_pos), data,  data_len);
		*buf_pos += data_len;
	}

	DLOG(CLIB_TLV_NOISY_DEBUG2, "put tlv: buf_pos=%ld tag=%x data_len=%ld tlv_len=%ld \n", (long int) *buf_pos, tag, (long int) data_len, (long int) tlv_len);

	return(tlv_len);
}


size_t _muacc_push_addrinfo_tlv( char *buf, size_t *buf_pos, size_t buf_len,
	muacc_tlv_t tag, const struct addrinfo *ai0)
{

    const struct addrinfo *ai;
	size_t data_len = 0; 
	size_t tlv_len = 0; 

	DLOG(CLIB_TLV_NOISY_DEBUG1, "invoked buf_pos=%ld buf_len=%ld ai=%p\n", (long) *buf_pos, (long) buf_len, (void *) ai0);

	if ( ai0 == NULL )
	{
		return 0;
	}
	
	/* calculate size */
    for (ai = ai0; ai != NULL; ai = ai->ai_next)
	{	
		size_t i = 0;

		i += sizeof(struct addrinfo);
		i += ai->ai_addrlen;
		if(ai->ai_canonname != NULL)
			i += strlen(ai->ai_canonname);

		DLOG(CLIB_TLV_NOISY_DEBUG2, "calculated  length of  addrinfo at %p is %ld\n", (void *) ai, (long) i);
		data_len += i;
	}
	
	DLOG(CLIB_TLV_NOISY_DEBUG2, "total data length is %ld\n", (long) data_len);

	/* check size */
	tlv_len = sizeof(muacc_tlv_t)+sizeof(size_t)+data_len;
	if (buf == NULL)
	{
		/* checking case */
		DLOG(CLIB_TLV_NOISY_DEBUG1, "length checking done - total length is %ld - returning\n", (long) data_len);
		return(tlv_len);
	}
	else if ( *buf_pos + tlv_len >= buf_len)
	{
		return(-1);
	}
	
	/* write tag */	
	*((muacc_tlv_t *) (buf + *buf_pos)) = tag;
	*buf_pos += sizeof(muacc_tlv_t);
	
	*((size_t *) (buf + *buf_pos)) = data_len;
	*buf_pos += sizeof(size_t);
	
	/* deep copy struct */
    for (ai = ai0; ai != NULL; ai = ai->ai_next)
	{	
		DLOG(CLIB_TLV_NOISY_DEBUG2, "copy addrinfo at %p to tlv buf_pos=%ld buf_len=%ld\n", (void *) ai, (long) *buf_pos, (long) buf_len);

		memcpy( (void *) (buf + *buf_pos), ai, sizeof(struct addrinfo));
		*buf_pos += sizeof(struct addrinfo);
		if ( ai->ai_addr != NULL) 
		{
			memcpy( (void *) (buf + *buf_pos), ai->ai_addr, ai->ai_addrlen);
			*buf_pos += ai->ai_addrlen;
		}
		if ( ai->ai_canonname != NULL) 
		{
			size_t sl = strlen(ai->ai_canonname)+1;
			*((size_t *) (buf + *buf_pos)) = sl;
			*buf_pos += sizeof(size_t);
			memcpy( (void *) (buf + *buf_pos), ai->ai_canonname, sl);
			*buf_pos += sl;
		}
	}	

	DLOG(CLIB_TLV_NOISY_DEBUG1, "done total length copied was %ld - returning\n", (long) tlv_len);

	return(tlv_len);
}

size_t _muacc_push_socketopt_tlv( char *buf, size_t *buf_pos, size_t buf_len,
	muacc_tlv_t tag, const struct socketopt *so0)
{

    const struct socketopt *so;
	size_t data_len = 0;
	size_t tlv_len = 0;

	DLOG(CLIB_TLV_NOISY_DEBUG1, "invoked buf_pos=%ld buf_len=%ld ai=%p\n", (long) *buf_pos, (long) buf_len, (void *) so0);

	if (so0 == NULL)
	{
		return 0;
	}

	/* calculate size */
    for (so = so0; so != NULL; so = so->next)
	{
		size_t i = sizeof(struct socketopt);

		if (so->optlen != 0 && so->optval != NULL)
			i += so->optlen;

		DLOG(CLIB_TLV_NOISY_DEBUG2, "calculated  length of  socketopt at %p is %ld\n", (void *) so, (long) i);
		data_len += i;
	}

    DLOG(CLIB_TLV_NOISY_DEBUG2, "total data length is %ld\n", (long) data_len);

	/* check size */
	tlv_len = sizeof(muacc_tlv_t)+sizeof(size_t)+data_len;
	if (buf == NULL)
	{
		/* checking case */
		DLOG(CLIB_TLV_NOISY_DEBUG1, "length checking done - total length is %ld - returning\n", (long) data_len);
		return(tlv_len);
	}
	else if ( *buf_pos + tlv_len >= buf_len)
	{
		return(-1);
	}

	/* write tag */
	*((muacc_tlv_t *) (buf + *buf_pos)) = tag;
	*buf_pos += sizeof(muacc_tlv_t);

	*((size_t *) (buf + *buf_pos)) = data_len;
	*buf_pos += sizeof(size_t);

	/* deep copy struct */
    for (so = so0; so != NULL; so = so->next)
	{
		memcpy((buf+ *buf_pos), so, sizeof(struct socketopt));
		*buf_pos += sizeof(struct socketopt);

		if (so->optlen != 0 && so->optval != NULL)
    	{
        	memcpy((buf+ *buf_pos), so->optval, so->optlen);
        	*buf_pos += so->optlen;
    	}
	}

	DLOG(CLIB_TLV_NOISY_DEBUG1, "done total length used was %ld - returning\n", (long) tlv_len);
	return(tlv_len);

}

size_t _muacc_extract_addrinfo_tlv( const char *data, size_t data_len, struct addrinfo **ai0)
{
	struct addrinfo **ai1 = ai0;

	size_t data_pos = 0;
	struct addrinfo *ai;

	size_t allocated = 0;

	DLOG(CLIB_TLV_NOISY_DEBUG1, "invoked data_len=%ld\n", (long) data_len);

	do
	{

		/* check length */
		if (data_len-data_pos < sizeof(struct addrinfo))
		{
			DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: data_len too short - data_pos=%ld data_len=%ld sizeof(struct addrinfo)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct addrinfo));
			*ai1 = NULL;
			goto muacc_extract_addrinfo_tlv_length_failed;
		}

		/* get memory and copy struct */
		if( (ai = malloc(sizeof(struct addrinfo))) == NULL )
			goto muacc_extract_addrinfo_tlv_malloc_failed;
		allocated += sizeof(struct addrinfo);
		memcpy( ai, (void *) (data + data_pos),sizeof(struct addrinfo));
		data_pos += sizeof(struct addrinfo);

		DLOG(CLIB_TLV_NOISY_DEBUG2, "copied addrinfo to %p\n", (void *) ai);

		/* addrinfo */
		if ( ai->ai_addr != NULL)
		{
			ai->ai_addr = NULL;

			/* check length again */
			if (data_len-data_pos < ai->ai_addrlen)
			{
				DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: data_len too short while extracting ai_addr - data_pos=%ld data_len=%ld sizeof(struct addrinfo)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct addrinfo));
				ai->ai_canonname = NULL;
				goto muacc_extract_addrinfo_tlv_length_failed;
			}
			/* get memory and copy struct */
			if( (ai->ai_addr = malloc(ai->ai_addrlen)) == NULL )
				goto muacc_extract_addrinfo_tlv_malloc_failed;
			allocated += ai->ai_addrlen;
			memcpy( ai->ai_addr,  (void *) (data + data_pos), ai->ai_addrlen);
			data_pos += ai->ai_addrlen;

			DLOG(CLIB_TLV_NOISY_DEBUG2, "copied addrinfo ai_addr to %p\n", (void *) ai->ai_addr);

		}

		/* ai_canonname */
		if ( ai->ai_canonname != NULL)
		{
			ai->ai_canonname = NULL;

			/* check length again */
			if (data_len-data_pos < sizeof(size_t))
			{
				DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: data_len too short while extracting ai_canonname_len - data_pos=%ld data_len=%ld sizeof(struct addrinfo)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct addrinfo));
				goto muacc_extract_addrinfo_tlv_length_failed;
			}
			/* get string length + trailing\0 */
			size_t canonname_len = *((size_t *) (data + data_pos));
			data_pos += sizeof(size_t);

			/* check length again */
			if (data_len-data_pos < canonname_len)
			{
				DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING data_len too short while extracting ai_canonname - data_pos=%ld data_len=%ld sizeof(struct addrinfo)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct addrinfo));
				goto muacc_extract_addrinfo_tlv_length_failed;
			}
			if( (ai->ai_canonname = malloc(canonname_len)) == NULL )
				goto muacc_extract_addrinfo_tlv_malloc_failed;
			allocated += canonname_len;
			memcpy( ai->ai_canonname, (void *) (data + data_pos), canonname_len);
			*((ai->ai_canonname)+canonname_len-1) = 0x00;
			data_pos += canonname_len;

			DLOG(CLIB_TLV_NOISY_DEBUG2, "copied addrinfo ai_canonname to %p (%s)\n", (void *) ai->ai_canonname, ai->ai_canonname);

		}

		/* fix pointers */
		*ai1 = ai;
		ai1 = &(ai->ai_next);

	} while (ai->ai_next != NULL);

	DLOG(CLIB_TLV_NOISY_DEBUG1, "done - %ld bytes allocated\n", (long) allocated);

    return allocated;

    muacc_extract_addrinfo_tlv_length_failed:
    freeaddrinfo(*ai0);
    *ai0 = NULL;
    return -1;

    muacc_extract_addrinfo_tlv_malloc_failed:
    *ai0 = NULL;
    return -1;



    *ai0 = NULL;
    return -1;

}

size_t _muacc_extract_socketaddr_tlv( const char *data, size_t data_len, struct sockaddr **sa0)
{

	size_t data_pos = 0;

	/* check length */
	if (data_len-data_pos < sizeof(struct addrinfo))
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: ata_len too short - data_pos=%ld data_len=%ld sizeof(struct addrinfo)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct addrinfo));
		return(-1);
	}

	/* get memory and copy struct */
	if( (*sa0 = malloc(data_len)) == NULL )
		goto muacc_extract_socketaddr_tlv_malloc_failed;
	memcpy( *sa0, (void *) (data + data_pos),data_len);
	data_pos += data_len;

	return(data_len);

	muacc_extract_socketaddr_tlv_malloc_failed:
	*sa0 = NULL;
	return(-1);

}

size_t _muacc_extract_socketopt_tlv( const char *data, size_t data_len, struct socketopt **so0)
{
	struct socketopt **so1 = so0;

	size_t data_pos = 0;
	struct socketopt *so;

	size_t allocated = 0;

	DLOG(CLIB_TLV_NOISY_DEBUG1, "invoked data_len=%ld\n", (long) data_len);

	do
	{
		/* for cleanup in _muacc_extract_socketopt_tlv_parse_failed */
		so = NULL;

		/* check length */
		if (data_len-data_pos < sizeof(struct socketopt))
		{
			DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: data_len too short - data_pos=%ld data_len=%ld sizeof(struct socketopt)=%ld\n", (long int) data_pos, (long int) data_len, (long int) sizeof(struct socketopt));
			*so1 = NULL;
			goto _muacc_extract_socketopt_tlv_parse_failed;
		}

		/* get memory and copy struct */
		if( (so = malloc(sizeof(struct socketopt))) == NULL )
			goto _muacc_extract_socketopt_tlv_malloc_failed;
		allocated += sizeof(struct socketopt);
		memcpy( so, (void *) (data + data_pos),sizeof(struct socketopt));
		data_pos += sizeof(struct socketopt);

		DLOG(CLIB_TLV_NOISY_DEBUG2, "copied socketaddr to %p\n", (void *) so);

		/* check and set option pointer */
		if(so->optlen > 0 && so->optval != NULL)
		{
			if(data_len-data_pos < so->optlen ) {
				DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: so->optlen too large for data_len - data_pos=%ld data_len=%ld so->optlen=%ld\n", (long int) data_pos, (long int) data_len, (long int) so->optlen);
				so->optval = NULL;
				so->next = NULL;
				goto _muacc_extract_socketopt_tlv_parse_failed;
			}

			if( (so->optval = malloc(so->optlen)) == NULL )
				goto _muacc_extract_socketopt_tlv_malloc_failed;

			memcpy(so->optval, (void *) (data + data_pos), so->optlen );
			data_pos += so->optlen;
			
			DLOG(CLIB_TLV_NOISY_DEBUG2, "copied %zd bytes of data to %p\n", so->optlen, (void *) so->optval);
		}

		/* weave pointer magic */
		*so1 = so;
		so1 = &(so->next);

	} while(so->next != NULL);

	DLOG(CLIB_TLV_NOISY_DEBUG1, "done - %ld bytes allocated\n", (long) allocated);

	return(allocated);

	_muacc_extract_socketopt_tlv_parse_failed:
	_muacc_free_socketopts(*so0);
	*so0 = NULL;
	return(-1);

	_muacc_extract_socketopt_tlv_malloc_failed:
	*so0 = NULL;
	return(-1);

}

int _muacc_send_ctx_event(struct _muacc_ctx *_ctx, muacc_mam_action_t reason)
{

	struct evbuffer_iovec v[1];
	size_t ret = 0;
	size_t pos = 0;

	/* Reserve space */
	DLOG(CLIB_TLV_NOISY_DEBUG2,"reserving buffer\n");

	ret = evbuffer_reserve_space(_ctx->out, MUACC_TLV_MAXLEN, v, 1);
	if(ret <= 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0,"ERROR reserving buffer\n");
		return(-1);
	}

	DLOG(CLIB_TLV_NOISY_DEBUG1, "packing request\n");

	/* pack request */
	if( 0 > _muacc_push_tlv(v[0].iov_base, &pos, v[0].iov_len, action, &reason, sizeof(muacc_mam_action_t)) ) goto  _muacc_send_ctx_event_pack_err;
	if( 0 > _muacc_pack_ctx(v[0].iov_base, &pos, v[0].iov_len, _ctx) ) goto  _muacc_send_ctx_event_pack_err;
	if( 0 > _muacc_push_tlv_tag(v[0].iov_base, &pos, v[0].iov_len, eof) ) goto  _muacc_send_ctx_event_pack_err;
	DLOG(CLIB_TLV_NOISY_DEBUG2,"packing request done\n");

   v[0].iov_len = pos;


	DLOG(CLIB_TLV_NOISY_DEBUG1,"committing buffer\n");
	if (evbuffer_commit_space(_ctx->out, v, 1) < 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0,"ERROR committing buffer\n");
	    return(-1); /* Error committing */
	}
	else
	{
		DLOG(CLIB_TLV_NOISY_DEBUG2,"committed buffer - finished sending request\n");
	    return(0);
	}

	_muacc_send_ctx_event_pack_err:
		return(-1);
}


int _muacc_proc_tlv_event(struct evbuffer *input, struct evbuffer *output, struct _muacc_ctx *_ctx )
{

	unsigned char *buf;
	size_t buf_pos = 0;

	size_t tlv_len;
	muacc_tlv_t *tag;
	void *data;
	size_t *data_len;

	/* check header */
	tlv_len = sizeof(muacc_tlv_t) + sizeof(size_t);
    buf = evbuffer_pullup(input, tlv_len);
	if(buf == NULL)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG1, "header read failed: buffer too small - please try again later\n");
		return(_muacc_proc_tlv_event_too_short);
	}
	assert(evbuffer_get_length(input) >= tlv_len );

	/* parse tag and length */
	tag = ((muacc_tlv_t *) (buf + buf_pos));
	buf_pos += sizeof(muacc_tlv_t);

	data_len = ((size_t *) (buf + buf_pos));
	buf_pos += sizeof(size_t);

	DLOG(CLIB_TLV_NOISY_DEBUG2, "read header - buf_pos=%ld tag=%x, data_len=%ld tlv_len=%ld \n" , (long int) buf_pos, *tag, (long int) *data_len, (long int) tlv_len);

	/* check eof */
	if(*tag == eof)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG2, "found eof - returning\n");
        evbuffer_drain(input, tlv_len);
		return(_muacc_proc_tlv_event_eof);
	}

	/* check data */
	tlv_len += *data_len;
    buf = evbuffer_pullup(input, tlv_len);
	if(buf == NULL)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG1, "header read failed: buffer too small - please try again later\n");
		return(_muacc_proc_tlv_event_too_short);
	}
	assert(evbuffer_get_length(input) >= tlv_len );
	data = ((void *) (buf + buf_pos));

	/* set context input/output buffer */
	_ctx->in  = input;
	_ctx->out = output;

	/* process tlv */
	switch( _muacc_unpack_ctx(*tag, data, *data_len, _ctx) )
	{
		case 0:
			DLOG(CLIB_TLV_NOISY_DEBUG1, "parsing TLV successful\n");
			break;
		default:
			DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: parsing TLV failed: tag=%d data_len=%ld\n", (int) *tag, (long) *data_len);
			break;
	}

    evbuffer_drain(input, tlv_len);
	return(tlv_len);

}


size_t _muacc_read_tlv( int fd,
	char *buf, size_t *buf_pos, size_t buf_len,
	muacc_tlv_t *tag,
	void **data, size_t *data_len)
{
	size_t tlv_len;
	size_t rlen, rrem;

	DLOG(CLIB_TLV_NOISY_DEBUG1, "invoked - buf_pos=%ld\n", (long int) *buf_pos);

	/* check size */
	if ( *buf_pos + sizeof(muacc_tlv_t) + sizeof(size_t) >= buf_len )
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: header read failed: buffer too small\n");
		goto muacc_read_tlv_err;
	}

	/* read header */
	rlen = read(fd, (buf + *buf_pos) , (sizeof(muacc_tlv_t) + sizeof(size_t)) );
	if(rlen <= 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "ERROR: header read failed: %s \n", strerror(errno));
		goto muacc_read_tlv_err;
	}
	else if(rlen < sizeof(muacc_tlv_t) + sizeof(size_t))
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: header read failed: short read\n");
		goto muacc_read_tlv_err;
	}

	/* parse tag and length */
	*tag = *((muacc_tlv_t *) (buf + *buf_pos));
	*buf_pos += sizeof(muacc_tlv_t);

	*data_len = *((size_t *) (buf + *buf_pos));
	*buf_pos += sizeof(size_t);

	tlv_len = sizeof(muacc_tlv_t) + sizeof(size_t) + *data_len;

	DLOG(CLIB_TLV_NOISY_DEBUG1, "read header - buf_pos=%ld tag=%x, data_len=%ld tlv_len=%ld \n" , (long int) *buf_pos, *tag, (long int) *data_len, (long int) tlv_len);

	/* check size again */
	if (*buf_pos + *data_len >= buf_len)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: read failed: buffer too small\n");
		goto muacc_read_tlv_err;
	}

	/* check EOF TLV */
	if( *tag == eof )
	{
		DLOG(CLIB_TLV_NOISY_DEBUG1, "found eof - returning\n");
		*data = NULL;
		*data_len = 0;
		return(tlv_len);
	}

	/* update data pointer */
	*data = ( (void *) (buf + *buf_pos));

	/* read data */
	rrem = *data_len;
	while(rrem > 0)
	{
		rlen = read(fd, buf + *buf_pos , rrem);
		if(rlen <= 0)
		{
			DLOG(CLIB_TLV_NOISY_DEBUG0, "ERROR: data read failed: %s \n", strerror(errno));
			goto muacc_read_tlv_err;
		}
		rrem     -= rlen;
		*buf_pos += rlen;
	}

	DLOG(CLIB_TLV_NOISY_DEBUG1, "read data done - buf_pos=%ld tag=%x, data_len=%ld tlv_len=%ld \n" , (long int) *buf_pos, *tag, (long int) *data_len, (long int) tlv_len);

	return(tlv_len);

muacc_read_tlv_err:

	*data = NULL;
	*data_len = -1;
	return(-1);

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
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: socket creation failed: %s\n", strerror(errno));
		return(-errno);
	}

	if(connect(ctx->mamsock, (struct sockaddr*) &mams, sizeof(mams)) < 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: connect to mam via %s failed: %s\n",  mams.sun_path, strerror(errno));
		close(ctx->mamsock);
		ctx->mamsock = -1;
		return(-errno);
	}

	return 0;
}


int _muacc_contact_mam (muacc_mam_action_t reason, muacc_context_t *ctx)
{

	char buf[MUACC_TLV_MAXLEN];
	size_t pos = 0;
	size_t ret = 0;
	muacc_tlv_t tag;
	void *data;
	size_t data_len;
	
	/* connect to MAM */
	if(	_muacc_connect_ctx_to_mam(ctx) != 0 )
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: failed to contact MAM\n");
		goto _muacc_contact_mam_connect_err;
	}

	DLOG(CLIB_TLV_NOISY_DEBUG1, "packing request\n");

	/* pack request */
	if( 0 > _muacc_push_tlv(buf, &pos, sizeof(buf), action, &reason, sizeof(muacc_mam_action_t)) ) goto  _muacc_contact_mam_pack_err;
	if( 0 > _muacc_pack_ctx(buf, &pos, sizeof(buf), ctx->ctx) ) goto  _muacc_contact_mam_pack_err;
	if( 0 > _muacc_push_tlv_tag(buf, &pos, sizeof(buf), eof) ) goto  _muacc_contact_mam_pack_err;
	DLOG(CLIB_TLV_NOISY_DEBUG2,"packing request done\n");


	/* send requst */
	if( 0 > (ret = send(ctx->mamsock, buf, pos, 0)) )
	{
		DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: error sending request: %s\n", strerror(errno));
		goto _muacc_contact_mam_connect_err;
	}
	else
 	{
		DLOG(CLIB_TLV_NOISY_DEBUG1, "request sent  - %ld of %ld bytes\n", (long int) ret, (long int) pos);
 	}

	/* read & unpack response */
	DLOG(CLIB_TLV_NOISY_DEBUG1, "processing response:\n");
	pos = 0;
	while( (ret = _muacc_read_tlv(ctx->mamsock, buf, &pos, sizeof(buf), &tag, &data, &data_len)) > 0)
	{
		DLOG(CLIB_TLV_NOISY_DEBUG2, "\tpos=%ld tag=%x, len=%ld\n", (long int) pos, tag, (long int) data_len);
		if( tag == eof )
			break;
		else if ( 0 > _muacc_unpack_ctx(tag, data, data_len, ctx->ctx) )
			goto  _muacc_contact_mam_parse_err;
	}
	DLOG(CLIB_TLV_NOISY_DEBUG2, "processing response done: pos=%li last_res=%li done\n", (long int) pos, (long int) ret);
	return(0);

_muacc_contact_mam_connect_err:
	return(-1);

_muacc_contact_mam_pack_err:

	DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: failed to pack request\n");
	return(-1);

_muacc_contact_mam_parse_err:

	DLOG(CLIB_TLV_NOISY_DEBUG0, "WARNING: failed to process response\n");
	return(-1);

}

