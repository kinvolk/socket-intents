/** \file testmuacc.c
 *  \brief Set of unit tests for the muacc library
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <glib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include "../clib/muacc.h"
#include "../clib/muacc_ctx.h"
#include "../clib/muacc_tlv.h"
#include "../clib/muacc_util.h"
#include "../libintents/libintents.h"
#include "../clib/dlog.h"

#ifndef TESTMUACC_NOISY_DEBUG
#define TESTMUACC_NOISY_DEBUG 0
#endif

/** Fixture = Element used in a set of tests
 *
 */
typedef struct
{
	muacc_context_t *context;
} dfixture;

/** Helper that creates an empty muacc context
 *
 */
void ctx_empty_setup(dfixture *df, const void *test_data)
{
	DLOG(TESTMUACC_NOISY_DEBUG, "\n===========\n");
	muacc_context_t *newctx = malloc(sizeof(muacc_context_t));
	df->context = newctx;
	muacc_init_context(df->context);
}

/** Helper that releases a context
 *
 */
void ctx_destroy(dfixture *df, const void *test_data)
{
	muacc_release_context(df->context);
	free(df->context);
	DLOG(TESTMUACC_NOISY_DEBUG, "\n===========\n");
}

/** Helper that compares two lists of sockopts
 *
 */
void compare_sockopts(const struct socketopt *a, const struct socketopt *b)
{
	while (a != NULL && b != NULL)
	{
		g_assert_cmpint(a->level, ==, b->level);
		g_assert_cmpint(a->optname, ==, b->optname);
		g_assert_cmpint(a->optlen, ==, b->optlen);
		g_assert( 0 == memcmp(a->optval, b->optval, a->optlen));
		a = a->next;
		b = b->next;
	}
}

/** Helper to print out the TLV buffer
 *  (Host byte order -> LSB first on many systems!)
 */
void tlv_print_buffer(char buf[], size_t buflen)
{
    printf("TLV buffer: ");
    for (int i = 0; i < buflen; i++)
    {
        printf("%02x ", (unsigned char) buf[i]);
    }
    printf("length %d \n", (int) buflen);
}

/** Compare tlv buffer with a value that was supposed to be written into it
 *  in host byte order
 */
void compare_tlv(char *buf, size_t buf_pos, size_t buf_len, const void *value, size_t value_len)
{
	const unsigned int *val = value;

	g_assert(buf_pos + value_len <= buf_len);
	DLOG(TESTMUACC_NOISY_DEBUG, "Comparing buffer with buf_pos %d, buf_len %d, value_len %d\n", buf_pos, buf_len, value_len);
	for (int i = 0; i < value_len; i++)
	{
		unsigned int mask = *(val + i/4) & (0xff << 8*i);
		DLOG(TESTMUACC_NOISY_DEBUG, "%08x %02x %08x %08x\n", (unsigned int) 0xff << 8*i, (unsigned char) buf[buf_pos+i], mask, mask >> 8*i);
		g_assert_cmphex((unsigned char) buf[buf_pos+i], ==, mask >> 8*i);
	}
}

/** Trying to create a context with a NULL pointer
 *  This should return -1, but not crash the application
 */
void ctx_create_null()
{
	int ret = -2;
	ret = muacc_init_context(NULL);
	g_assert_cmpint(ret, ==, -1);
}

/** Test that prints out a context
 *
 */
void ctx_print(dfixture *df, const void *param)
{
	muacc_print_context(df->context);
}

/** Test that copies a list of sockopts
 *
 */
void sockopts_copy_valid()
{
	struct socketopt testopt = { .level = SOL_SOCKET, .optname = SO_BROADCAST, .optval=malloc(sizeof(int)), .optlen = sizeof(int) };
	int flag = 1;
	memcpy(testopt.optval, &flag, sizeof(int));

	struct socketopt testopt2 = { .level = SOL_INTENTS, .optname = SO_CATEGORY, .optval=malloc(sizeof(enum category)), .optlen = sizeof(enum category) };
	enum category cat = C_KEEPALIVES;
	memcpy(testopt2.optval, &cat, sizeof(enum category));
	testopt.next = &testopt2;

	struct socketopt *newopt = NULL;
	newopt = _muacc_clone_socketopts((const struct socketopt *) &testopt);
	compare_sockopts(&testopt, newopt);
	if (TESTMUACC_NOISY_DEBUG) _muacc_print_socket_option_list((const struct socketopt *) newopt);
}

/** Test that checks if a tag is pushed correctly to the buffer
 *  Buffer should then contain the tag in host byte order,
 *  a length of 0 and no data
 */
void tlv_push_tag()
{
    char buf[MUACC_TLV_MAXLEN];
    size_t writepos = 0;
    size_t buflen = 0;
	size_t readpos = 0;

    muacc_tlv_t label = 0x12345678;
	size_t valuelen = 0;

    DLOG(TESTMUACC_NOISY_DEBUG, "Pushing label %x of length %d\n", (unsigned int) label, sizeof(muacc_tlv_t));

    buflen = _muacc_push_tlv_tag(buf, &writepos, sizeof(buf), label);

    if (TESTMUACC_NOISY_DEBUG) tlv_print_buffer(buf, buflen);
    compare_tlv(buf, readpos, buflen, (const void *) &label, sizeof(muacc_tlv_t));
	readpos += sizeof(muacc_tlv_t);
	compare_tlv(buf, readpos, buflen, (const void *) &valuelen, sizeof(size_t));
}

/** Test that checks if a value is pushed correctly to the buffer
 *  Buffer should then contain the tag, the length of the value 
 *  and the value itself in host byte order
 */
void tlv_push_value()
{
    char buf[MUACC_TLV_MAXLEN];
    size_t writepos = 0;
    size_t buflen = 0;
	size_t readpos = 0;

	muacc_tlv_t label = action;
    muacc_mam_action_t reason = 0xcaffe007;
	size_t valuelen = sizeof(muacc_mam_action_t);

    DLOG(TESTMUACC_NOISY_DEBUG, "Pushing label %x value %x length %x\n", (unsigned int) action, (unsigned int) reason, (unsigned int) sizeof(muacc_mam_action_t));

    buflen = _muacc_push_tlv(buf, &writepos, sizeof(buf), label, &reason, sizeof(muacc_mam_action_t));

    if (TESTMUACC_NOISY_DEBUG) tlv_print_buffer(buf, buflen);

	compare_tlv(buf, readpos, buflen, (const void *) &label, sizeof(muacc_tlv_t));
	readpos += sizeof(muacc_tlv_t);
	compare_tlv(buf, readpos, buflen, (const void *) &valuelen, sizeof(size_t));
	readpos += sizeof(size_t);
	compare_tlv(buf, readpos, buflen, (const void *) &reason, sizeof(muacc_mam_action_t));

}

void tlv_push_socketopt()
{
	char buf[MUACC_TLV_MAXLEN];
    size_t writepos = 0;
	size_t readpos = 0;
    size_t buflen = 0;
	size_t valuelen = 0;

	muacc_tlv_t label = 0x01020304;

	const struct socketopt testopt = { .level = SOL_INTENTS, .optname = SO_CATEGORY, .optval=malloc(sizeof(enum category)), .optlen = sizeof(enum category) };
	enum category cat = C_KEEPALIVES;
	memcpy(testopt.optval, &cat, sizeof(enum category));

	buflen = _muacc_push_socketopt_tlv(buf, &writepos, sizeof(buf), label, &testopt);
	valuelen = sizeof(struct socketopt) + testopt.optlen;

	if (TESTMUACC_NOISY_DEBUG)
	{
		printf("buflen = %d, valuelen = %d [hex: %08x]\n", buflen, valuelen, valuelen);
		printf("sizeof(socketopt) = %d, optlen = %d\n", sizeof(struct socketopt), testopt.optlen);
		printf("{ level = %08x, optname = %08x, optlen = %08x, *optval = %08x, optval = %08x, next = %08x }\n", (unsigned int) testopt.level, (unsigned int) testopt.optname, (unsigned int) testopt.optlen, (unsigned int) testopt.optval, *(unsigned int*) testopt.optval, (unsigned int) testopt.next);
		tlv_print_buffer(buf, buflen);
	}

	compare_tlv(buf, readpos, buflen, (const void *) &label, sizeof(muacc_tlv_t));
	readpos += sizeof(muacc_tlv_t);
	compare_tlv(buf, readpos, buflen, (const void *) &valuelen, sizeof(size_t));
	readpos += sizeof(size_t);
	compare_tlv(buf, readpos, buflen, (const void *) &testopt, sizeof(struct socketopt));
	readpos += sizeof(struct socketopt);
	compare_tlv(buf, readpos, buflen, (const void *) testopt.optval, testopt.optlen);
}

/** Add test cases to the test harness */
int main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	DLOG(TESTMUACC_NOISY_DEBUG, "Welcome to the muacc testing functions\n");
	printf("================================================\n");
//	g_test_add("/ctx/print_empty", dfixture, NULL, ctx_empty_setup, ctx_print, ctx_destroy);
	g_test_add_func("/ctx/create_null", ctx_create_null);
	g_test_add_func("/sockopts/copy", sockopts_copy_valid);
	g_test_add_func("/tlv/push_value", tlv_push_value);
	g_test_add_func("/tlv/push_tag", tlv_push_tag);
	g_test_add_func("/tlv/push_socketopt", tlv_push_socketopt);
	return g_test_run();
}