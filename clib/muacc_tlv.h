#ifndef __MUACC_TLV_H__
#define __MUACC_TLV_H__ 1

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>
#include "muacc_types.h"


#define MUACC_TLV_MAXLEN 2048


/** push data in an TLV buffer
 *
 * @return length of the added tlv, -1 if there was an error.
 */
size_t _muacc_push_tlv (
	char *buf,         /**< [in]	 pointer to buffer to put data */
	size_t *buf_pos,   /**< [in,out] pointer to current offset to which the buffer is already used (in/out) */
	size_t buf_len,    /**< [in]	 length of the buffer */
	muacc_tlv_t tag,   /**< [in]	 tag of the data */
	const void *data,  /**< [in]	 data to be pushed into the buffer */
	size_t data_len    /**< [in]	 lengh of data to be pushed into the buffer */
);

/** push flag in an TLV buffer
 *
 * @return length of the added tlv, -1 if there was an error.
 */
size_t _muacc_push_tlv_tag( 
	char *buf,         /**< [in]     	pointer to buffer to put data */
	size_t *buf_pos,   /**< [in,out]    pointer to current offset to which the buffer is already used */
	size_t buf_len,    /**< [in]    	length of the buffer */
	muacc_tlv_t tag    /**< [in]    	tag to push */
);

/** push socketopt in a TLV buffer
 *
 *  @return length of the added tlv, -1 if there was an error.
 */
size_t _muacc_push_socketopt_tlv(
	char *buf,					/**< [in]		pointer to buffer to put data */
	size_t *buf_pos,			/**< [in,out]	pointer to current offset to which the buffer is already used */
	size_t buf_len,				/**< [in]		length of the buffer */
	muacc_tlv_t tag,			/**< [in]		tag of the data*/
	const struct socketopt *so0	/**< [in]		list of socketopts to push */
);

/** make a deep copy of the addrinfo and encode in TLV
 *
 * data is arranged as followed:
 * 			struct addrinfo ai
 *			struct sockaddr ai->ai_addr      if ai->ai_addr != NULL, length known from ai->ai_addrlen
 * 			size_t strlen(ai->ai_canonname)  if ai->ai_canonname != NULL
 *          string ai->ai_canonname			 if ai->ai_canonname != NULL
 *          ... next struct if ai->ai_next != NULL ...
 *
 * @return length of the TLV or -1 on error
 */
size_t _muacc_push_addrinfo_tlv (
	char *buf,          		/**< [in]     buffer to copy TLV into */
	size_t *buf_pos,    		/**< [in,out] position of next free space in the buffer */
	size_t buf_len,     		/**< [in]     length of the buffer */
	muacc_tlv_t tag,    		/**< [in]     tag of the data */
	const struct addrinfo *ai0	/**< [in]     addrinfo sruct do encode */
);

/** decode an encoded socketaddr 
 *
 * @return size of the extracted struct
 */
size_t _muacc_extract_socketaddr_tlv(
	const char *data,           /**< [in]     buffer to extract from */
	size_t data_len,            /**< [in]     length of data */
	struct sockaddr **sa0       /**< [out]    pointer to extracted struct (will be allocated) */
);

/** decode an encoded addrinfo by deep copying 
 *
 * @return sum of the sizes of the extracted structs/stringd
 */
size_t _muacc_extract_addrinfo_tlv(
	const char *data,           /**< [in]     buffer to extract from */
	size_t data_len,            /**< [in]     length of data */
	struct addrinfo **ai0       /**< [out]    pointer to extracted struct (will be allocated) */
);

/** decode an encoded socketopt by deep copying
 *
 * @return sum of the sizes of the extracted structs/stringd
 */
size_t _muacc_extract_socketopt_tlv(
	const char *data,           /**< [in]     buffer to extract from */
	size_t data_len,            /**< [in]     length of data */
	struct socketopt **so0       /**< [out]    pointer to extracted struct (will be allocated) */
);

#define _muacc_proc_tlv_event_too_short	-1
#define _muacc_proc_tlv_event_eof		0
/** try to read a TLV from an libevent2 evbuffer
 *
 * @return number of bytes processed, 0 if last TLV was EOF, -1 if buffer was too short
 */
int _muacc_proc_tlv_event(
	struct evbuffer *input,		/**< [in]	  evbuffer to read from */
	struct evbuffer *output,	/**< [in]	  evbuffer to write to  */
	struct _muacc_ctx *_ctx		/**< [in]	  ctx to extract data to */
);

/** read a TLV from a file descriptor
 *
 * @return length of the tlv read, -1 if there was an error.
 */
size_t _muacc_read_tlv(
	int fd,           	/**< [in]     file descriptor to read from */
 	char *buf,        	/**< [in]     pointer to buffer to put data */
	size_t *buf_pos,  	/**< [in,out] pointer to current offset to which the buffer is already used (in/out) */
	size_t buf_len,   	/**< [in]     length of the buffer */
 	muacc_tlv_t *tag, 	/**< [out]    tag extracted  */
 	void **data,      	/**< [out]    data extracted (pointer within buf) */
	size_t *data_len  	/**< [out]    length of data extracted */
);

/** send context via libevent evbuffer
 *
 * @return 0 on success, -1 if there was an error.
 */
int _muacc_send_ctx_event(struct _muacc_ctx *_ctx, muacc_mam_action_t reason);

/** speak the TLV protocol as a client to make MAM update _ctx with her wisdom
 *
 * @return 0 on success, a negative number otherwise
 */
int _muacc_contact_mam (
	muacc_mam_action_t reason,	/**< [in]	reason for contacting */
	muacc_context_t *ctx		/**< [in]	context to be updated */
);

/** make the TLV client ready by establishing a connection to MAM
 *
 * @return 0 on success, a negative number otherwise
 */
int _muacc_connect_ctx_to_mam(muacc_context_t *ctx) ;

#endif
