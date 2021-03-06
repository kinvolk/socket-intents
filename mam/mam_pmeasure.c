/** \file mam_pmeasure.c
 *
 *  \copyright Copyright 2013-2015 Philipp S. Tiesel, Theresa Enghardt, and Mirko Palmer.
 *  All rights reserved. This project is released under the New BSD License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <err.h>
#include <assert.h>
#include <getopt.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>

#include <glib.h>
#include "mam.h"
#include "mam_pmeasure.h"

#include "muacc_util.h"
#include "dlog.h"

#ifndef MAM_PMEASURE_LOGPREFIX
#define MAM_PMEASURE_LOGPREFIX "/tmp/metrics"
#endif

#ifdef HAVE_LIBNL
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/route/addr.h>
#include <netlink/route/link.h>
#include <netlink/idiag/idiagnl.h>
#include <netlink/idiag/vegasinfo.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>
#endif

#ifndef MAM_PMEASURE_NOISY_DEBUG0
#define MAM_PMEASURE_NOISY_DEBUG0 0
#endif

#ifndef MAM_PMEASURE_NOISY_DEBUG1
#define MAM_PMEASURE_NOISY_DEBUG1 0
#endif

#ifndef MAM_PMEASURE_NOISY_DEBUG2
#define MAM_PMEASURE_NOISY_DEBUG2 0
#endif

void compute_srtt(void *pfx, void *data);

int compare_ip (struct sockaddr *a1, struct sockaddr *a2);
int is_addr_in_pfx (const void *a, const void *b);

void compute_median(GHashTable *dict, GList *values);
void compute_mean(GHashTable *dict, GList *values);
void compute_minimum(GHashTable *dict, GList *values);

#ifdef HAVE_LIBNL

#define BUFFER_SIZE (getpagesize() < 8192L ? getpagesize() : 8192L)
#define TCPF_ALL 0xFFF

void get_stats(void *pfx, void *data);
int create_nl_sock();
GList * parse_nl_msg(struct inet_diag_msg *pMsg, int rtalen, void *pfx, GList *values);
int send_nl_msg(int sock, int i);
int recv_nl_msg(int sock, void *pfx, GList **values);
void insert_errors(GHashTable *pTable, struct rtnl_link *pLink);
#endif

// The interval in which the computation of the values happens, i.e. the time between two computations (in seconds)
#ifndef CALLBACK_DURATION
static const double CALLBACK_DURATION=0.1;
#endif

// The number of samples that are collected before a maximum rate is computed
#ifndef MAX_SAMPLE
static const int MAX_SAMPLE=30;
#endif

// The smoothing factor for the rates (i.e. the weight of the new value)
#ifndef SMOOTH_FACTOR
static const double SMOOTH_FACTOR = 0.125;
#endif

// The smoothing factor for the maximum rates (i.e. the weight of the new value)
#ifndef SMOOTH_FACTOR_M
static const double SMOOTH_FACTOR_M = 0.125;
#endif

#ifdef IS_LINUX
//path of statistics file (sans interface) in linux broken into two strings
#ifndef path1
static const char path1[] = "/sys/class/net/";
#endif

#ifndef path2
static const char path2[] = "/statistics/";
#endif

long read_stats(char *path);
#endif

#ifndef MAM_PMEASURE_THRUPUT_DEBUG
#define MAM_PMEASURE_THRUPUT_DEBUG 0
#endif

void compute_link_usage(void *ifc, void *lookup);

// Alpha Value for Smoothed RTT Calculation
double alpha = 0.9;

/** compare two ip addresses
 *  return 0 if equal, non-zero otherwise
 */
int compare_ip (struct sockaddr *a1, struct sockaddr *a2)
{
    if (a1->sa_family != a2->sa_family)
        return 1;
    else
    {
        if (a1->sa_family == AF_INET) {
            return memcmp(&((struct sockaddr_in *)a1)->sin_addr, &((struct sockaddr_in *)a2)->sin_addr, sizeof(struct in_addr));; }
        else if (a1->sa_family == AF_INET6)
        {
            return memcmp(&((struct sockaddr_in6 *)a1)->sin6_addr, &((struct sockaddr_in6 *)a2)->sin6_addr, sizeof(struct in6_addr));
        }
    }
    return -1;
}

/** Checks whether a prefix contains a given sockaddr
 *  Returns 0 in this case
 */
int is_addr_in_pfx (const void *a, const void *b)
{
    const struct src_prefix_list *pfx = a;
    const struct sockaddr *addr = b;

    if (pfx == NULL || addr == NULL)
        return -2;

    struct sockaddr_list *addr_list = pfx->if_addrs;

    if (addr_list == NULL)
        return -2;

    for (; addr_list != NULL; addr_list = addr_list->next)
    {
        if (compare_ip((struct sockaddr *)addr, (struct sockaddr *) addr_list->addr) == 0)
            return 0;
    }
    return -1;
}

/** Compute the mean SRTT from the currently valid srtts
 *  Insert it into the measure_dict as "srtt_mean"
 */
void compute_mean(GHashTable *dict, GList *values)
{

    double *meanvalue;
    double old_rtt;

    int n = g_list_length(values);
    DLOG(MAM_PMEASURE_NOISY_DEBUG2, "List for interface has length %d\n", n);

    meanvalue = g_hash_table_lookup(dict, "srtt_mean");

    if (meanvalue == NULL)
    {
        meanvalue = malloc(sizeof(double));
        memset(meanvalue, 0, sizeof(double));
        g_hash_table_insert(dict, "srtt_mean", meanvalue);
    }

    old_rtt = *meanvalue;

    if (n == 0)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "No new RTT values. Keeping old mean %f\n", old_rtt);
        return;
    }

	double sum_of_values = 0;

    for (int i = 0; i < n; i++)
    {
        sum_of_values += *(double *) values->data;
        values = values->next;
    }

    *meanvalue = sum_of_values / n;
    DLOG(MAM_PMEASURE_NOISY_DEBUG2, "List of length %d has mean value %f \n", n, *meanvalue);

	if (old_rtt == 0)
	{
		DLOG(MAM_PMEASURE_NOISY_DEBUG2, "New mean value is %f \n", *meanvalue);
	}
	else
	{
		// calculate SRTT in accord with the formula
		// SRTT = (alpha * SRTT) + ((1-alpha) * RTT)
		// see RFC793
		*meanvalue = (alpha * *meanvalue) + ((1-alpha) * old_rtt);
		DLOG(MAM_PMEASURE_NOISY_DEBUG2, "New smoothed mean value is %f \n", *meanvalue);
	}
}

/** Compute the median SRTT from a table of individual flows with their SRTTs
 *  Insert it into the measure_dict as "srtt_median"
 */
void compute_median(GHashTable *dict, GList *values)
{
    double *medianvalue;

    int n;

    n = g_list_length(values);

    medianvalue = g_hash_table_lookup(dict, "srtt_median");

    if (medianvalue == NULL)
    {
        medianvalue = malloc(sizeof(double));
        memset(medianvalue, 0, sizeof(double));
        g_hash_table_insert(dict, "srtt_median", medianvalue);
    }

	double old_rtt = *medianvalue;

    if (n == 0)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "No new RTT values. Keeping old median %f\n", old_rtt);
        return;
    }
    else if (n % 2)
    {
        // odd number of elements
        *medianvalue = *(double *) g_list_nth_data(values, (n/2));
    }
    else
    {
        // even number of elements
        double val1 = *(double *) g_list_nth_data(values, (n/2)-1);
        double val2 = *(double *) g_list_nth_data(values, (n/2));
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "(intermediate value between %d. element %f and %d. element %f)\n",(n-1)/2, val1, (n+1)/2, val2);
        *medianvalue = (val1 + val2) / 2;
    }

	if (old_rtt == 0)
	{
		DLOG(MAM_PMEASURE_NOISY_DEBUG2, "New median value is %f \n", *medianvalue);
	}
	else
	{
		// calculate SRTT in accord with the formula
		// SRTT = (alpha * SRTT) + ((1-alpha) * RTT)
		// see RFC793
		*medianvalue = (alpha * *medianvalue) + ((1-alpha) * old_rtt);
		DLOG(MAM_PMEASURE_NOISY_DEBUG2, "New smoothed median value is %f \n", *medianvalue);
	}
}

void compute_minimum(GHashTable *dict, GList *values)
{
    double *minimum;

    int n;

    n = g_list_length(values);

    minimum = g_hash_table_lookup(dict, "srtt_minimum");

    if (minimum == NULL)
    {
        minimum = malloc(sizeof(double));
        memset(minimum, 0, sizeof(double));
        g_hash_table_insert(dict, "srtt_minimum", minimum);
    }

	double old_rtt = *minimum;
    if (n == 0)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "No new RTT values. Keeping old minimum %f\n", old_rtt);
        return;
    }
    else
    {
        *minimum = *(double *) g_list_first(values)->data;
		if (old_rtt == 0 || *minimum < old_rtt)
		{
			DLOG(MAM_PMEASURE_NOISY_DEBUG2, "New minimum value: %f \n", *minimum);
		}
		else
		{
			DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Keeping old minimum value %f (=< %f) \n", old_rtt, *minimum);
			*minimum = old_rtt;
		}
    }

}

#ifdef HAVE_LIBNL
void insert_errors(GHashTable *dict, struct rtnl_link *link)
{
    uint64_t *tx_errors;
    uint64_t *rx_errors;

    tx_errors = g_hash_table_lookup(dict, "tx_errors");
    rx_errors = g_hash_table_lookup(dict, "rx_error");

    if (tx_errors == NULL)
    {
        tx_errors = malloc(sizeof(uint64_t));
        memset(tx_errors, 0, sizeof(uint64_t));
        g_hash_table_insert(dict, "tx_errors", tx_errors);
    }

    if (rx_errors == NULL)
    {
        rx_errors = malloc(sizeof(uint64_t));
        memset(rx_errors, 0, sizeof(uint64_t));
        g_hash_table_insert(dict, "rx_errors", rx_errors);
    }

    *tx_errors = rtnl_link_get_stat(link,RTNL_LINK_TX_ERRORS);
    DLOG(MAM_PMEASURE_NOISY_DEBUG2,"Added %" PRIu64 " as TX_ERRORS\n", *tx_errors);
    *rx_errors = rtnl_link_get_stat(link,RTNL_LINK_RX_PACKETS);
    DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Added %" PRIu64 " as RX_ERRORS\n", *rx_errors);
}
#endif

#ifdef IS_LINUX
/**
 *This function reads reads the interface counters for each interface called.
 */

long read_stats(char *path)
{
    FILE *fp;
    long curr_counter = 0;

    fp = fopen((const char *)path,"r");

    if (fp == NULL)
    {    DLOG(MAM_PMEASURE_THRUPUT_DEBUG, "\nError Reading stats file\n");
        perror(path);
    }
    fscanf(fp,"%ld",&curr_counter);
    fclose(fp);
    return curr_counter;
}
#endif

/**
 *This function computes the link usage for each interface. Many key values are stored in the dictonary of each interface.
 For the upload and download activity on the interface:
 The previous counter value with the key "upload_counter" and "download_counter"
 The data rate in the 10 second duration with the key "upload_rate" and "download_rate"
 The smoothed data rate which is a function of data rate(prev line) and previously calculated smoothed data rate with keys "upload_srate"
 and "download_srate"
 The function also observes the maximum data rate reached on each interface in a partical sample period(currently 5 min) They are stored
 with the keys "upload_max_rate" and "download_max_rate"
 Finally, The smoothed maximal data rate is calulated(from periodic maximal rates and previous smoothed maximal data rate)
 */
void compute_link_usage(void *ifc, void *lookup)
{
	#ifdef IS_LINUX
    struct iface_list *iface = ifc;
    char path[100];

    long curr_bytes;
    double curr_rate;
    double curr_srate;
    double curr_MSrate;
    long *prev_bytes;
    double *prev_rate;
    double *prev_srate;
    double *prev_Mrate;
    double *prev_MSrate;

    int *prev_sample;

    if (iface == NULL){
        return;
    }

    if(strcmp(iface->if_name,"lo"))
    {
        DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"\n\n==========\tINTERFACE %s\t==========\n", iface->if_name);

        /******************************************************************************
         ****************    Upload Activity
         ******************************************************************************/
        //creating path for tx_bytes starts
        sprintf(path,"%s%s%s%s",path1,iface->if_name,path2,"tx_bytes");
        //creating path for tx_bytes ends
        DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"=========\tUPLOAD STATS\t=========\n");

        //reading last counter from dictionary starts
        prev_bytes = g_hash_table_lookup(iface->measure_dict, "upload_counter");
        prev_rate = g_hash_table_lookup(iface->measure_dict, "upload_rate");
        prev_srate = g_hash_table_lookup(iface->measure_dict, "upload_srate");
        prev_Mrate = g_hash_table_lookup(iface->measure_dict,"upload_max_rate");
        prev_sample = g_hash_table_lookup(iface->measure_dict,"sample");
        //reading last counter from dictionary ends

        //reading interface counter starts
        curr_bytes = read_stats(path);
        //reading interface counter ends
        if(prev_bytes){
            (*prev_sample)++;
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Sample Number: %d\n",*prev_sample);
            curr_rate = (curr_bytes - *prev_bytes)/CALLBACK_DURATION;

            //calculating smooth upload rate starts
            curr_srate = SMOOTH_FACTOR*(curr_rate) + (1-SMOOTH_FACTOR)*(*prev_srate);
            //calculating smooth upload rate ends

            *prev_rate = curr_rate;
            *prev_srate = curr_srate;
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Updating the dictionary\n");
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Current Counter Value: %ld Bytes\n",curr_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Previous Counter Value: %ld Bytes\n",*prev_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Activity: %ld Bytes\n",(curr_bytes - *prev_bytes));
            *prev_bytes = curr_bytes;
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Upload Link Usage: %f Bps\n",*prev_rate);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Smoothed Upload Link Usage: %f Bps\n",*prev_srate);

            //Periodic Maximal Data rate determination starts

            //Check if a new maximum data rate has been achieved in the sample period.
            if (curr_rate > *prev_Mrate){
                *prev_Mrate = curr_rate;
                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"New Max. upload rate reached: %.3fbps\n",*prev_Mrate);
            }

            //Check if the end of the sample period has been reached.
            if (*prev_sample == MAX_SAMPLE){

                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"End of Sample duration\n");

                //fetch the previous maximal smoothed rate (0.0 for the first sample period)
                prev_MSrate = g_hash_table_lookup(iface->measure_dict,"upload_max_srate");
                //determine the newest smoothed maximam data rate.
				if (*prev_MSrate == 0)
				{
					curr_MSrate = *prev_Mrate;
				}
				else
				{
					curr_MSrate = SMOOTH_FACTOR_M*(*prev_Mrate) + (1-SMOOTH_FACTOR_M)*(*prev_MSrate);
				}
                *prev_MSrate = curr_MSrate;

                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"The Max. upload rate of this sample period: %.3fbps\n",*prev_Mrate);
                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"The new smooth Max. upload rate: %.3fbps\n",*prev_MSrate);
                *prev_Mrate = curr_rate;
            }
            //periodic maximal ends
        }
        else {
            //initialization during the first run for a particular interface
            int *sample = malloc(sizeof(int));

            long *t_bytes = malloc(sizeof(long));
            double *upload_rate = malloc(sizeof(double));
            double *s_up_rate = malloc(sizeof(double));
            double *period_up_rate_max = malloc(sizeof(double));
            double *period_up_rate_smooth = malloc(sizeof(double));
            double *callback_duration_rate = malloc(sizeof(double));

            *sample = 0;
            *t_bytes = curr_bytes;
            *upload_rate = 0.0;
            *s_up_rate = 0.0;
            *period_up_rate_max = 0.0;
            *period_up_rate_smooth = 0.0;
            *callback_duration_rate = CALLBACK_DURATION;

            prev_sample = sample;

            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Sample Number: %d\n",*sample);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Inserting to the dictionary\n");
            g_hash_table_insert(iface->measure_dict, "sample",sample);
            g_hash_table_insert(iface->measure_dict, "upload_counter",t_bytes);
            g_hash_table_insert(iface->measure_dict, "upload_rate",upload_rate);
            g_hash_table_insert(iface->measure_dict, "upload_max_rate",period_up_rate_max);
            g_hash_table_insert(iface->measure_dict, "upload_srate",s_up_rate);
            g_hash_table_insert(iface->measure_dict, "upload_max_srate",period_up_rate_smooth);
            g_hash_table_insert(iface->measure_dict, "callback_duration_rate",callback_duration_rate);

            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Current Counter Value: %ld Bytes\n",*t_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Upload Link Usage: %f Bps\n",*upload_rate);
        }

        prev_bytes = NULL;
        prev_rate = NULL;
        prev_srate = NULL;
        prev_Mrate = NULL;

        /******************************************************************************
         ****************    Download Activity
         ******************************************************************************/
        //creating path for rx_bytes starts
        sprintf(path,"%s%s%s%s",path1,iface->if_name,path2,"rx_bytes");
        //creating path for rx_bytes ends

        DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"=========\tDOWNLOAD STATS\t=========\n");
        //reading last counter from dictionary starts
        prev_bytes = g_hash_table_lookup(iface->measure_dict, "download_counter");
        prev_rate = g_hash_table_lookup(iface->measure_dict, "download_rate");
        prev_srate = g_hash_table_lookup(iface->measure_dict, "download_srate");
        prev_Mrate = g_hash_table_lookup(iface->measure_dict,"download_max_rate");
        //reading last counter from dictionary ends

        //reading interface counter starts
        curr_bytes = read_stats(path);
        //reading interface counter ends
        if(prev_bytes){
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Sample Number: %d\n",*prev_sample);
            curr_rate = (curr_bytes - *prev_bytes)/CALLBACK_DURATION;

            //calculating smooth download rate starts
            curr_srate = SMOOTH_FACTOR*(curr_rate) + (1-SMOOTH_FACTOR)*(*prev_srate);
            //calculating smooth download rate ends

            *prev_rate = curr_rate;
            *prev_srate = curr_srate;
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Updating the dictionary\n");
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Current Counter Value: %ld Bytes\n",curr_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Previous Counter Value: %ld Bytes\n",*prev_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Activity: %ld Bytes\n",(curr_bytes - *prev_bytes));
            *prev_bytes = curr_bytes;
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Download Link Usage: %f Bps\n",*prev_rate);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Smoothed Download Link Usage: %f Bps\n",*prev_srate);

            //Periodic Maximal Data rate determination starts

            //Check if a new maximum data rate has been achieved in the sample period.
            if (curr_rate > *prev_Mrate){
                *prev_Mrate = curr_rate;
                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"New Max. download rate reached: %.3fbps\n",*prev_Mrate);
            }

            //Check if the end of the sample period has been reached.
            if (*prev_sample == MAX_SAMPLE){

                *prev_sample = 0;
                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"End of Sample duration\n");
                //fetch the previous maximal smoothed rate (0.0 for the first sample period)
                prev_MSrate = g_hash_table_lookup(iface->measure_dict,"download_max_srate");
                //determine the newest smoothed maximam data rate.
				if (*prev_MSrate == 0)
				{
					curr_MSrate = *prev_Mrate;
				}
				else
				{
					curr_MSrate = SMOOTH_FACTOR_M*(*prev_Mrate) + (1-SMOOTH_FACTOR_M)*(*prev_MSrate);
				}
                *prev_MSrate = curr_MSrate;

                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"The Max. download rate of this sample period: %.3fbps\n",*prev_Mrate);
                DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"The new smooth Max. download rate: %.3fbps\n",*prev_MSrate);
                *prev_Mrate = curr_rate;
            }
            //periodic maximal ends
        }
        else {
            //initialization during the first run for a particular interface
            long *r_bytes = malloc(sizeof(long));
            double *download_rate = malloc(sizeof(double));
            double *s_download_rate = malloc(sizeof(double));
            double *period_down_rate_max = malloc(sizeof(double));
            double *period_down_rate_smooth = malloc(sizeof(double));

            *r_bytes = curr_bytes;
            *download_rate = 0.0;
            *s_download_rate = 0.0;
            *period_down_rate_max = 0.0;
            *period_down_rate_smooth = 0.0;

            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Sample Number: %d\n",*prev_sample);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Inserting to the dictionary\n");
            g_hash_table_insert(iface->measure_dict, "download_counter",r_bytes);
            g_hash_table_insert(iface->measure_dict, "download_rate",download_rate);
            g_hash_table_insert(iface->measure_dict, "download_max_rate",period_down_rate_max);
            g_hash_table_insert(iface->measure_dict, "download_srate",s_download_rate);
            g_hash_table_insert(iface->measure_dict, "download_max_srate",period_down_rate_smooth);

            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Current Counter Value: %ld Bytes\n",*r_bytes);
            DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Upload Link Usage: %f Bps\n",*download_rate);
        }

		// Get timestamp and log it
		struct timeval current_time;
		gettimeofday(&current_time, NULL);
		double *measurement_timestamp_sec = g_hash_table_lookup(iface->measure_dict,"rate_timestamp_sec");
		double *measurement_timestamp_usec = g_hash_table_lookup(iface->measure_dict,"rate_timestamp_usec");

		if (measurement_timestamp_sec == NULL || measurement_timestamp_usec == NULL)
		{
			measurement_timestamp_sec = malloc(sizeof(double));
			memset(measurement_timestamp_sec, 0, sizeof(double));
			g_hash_table_insert(iface->measure_dict, "rate_timestamp_sec", measurement_timestamp_sec);
			*measurement_timestamp_sec = current_time.tv_sec;

			measurement_timestamp_usec = malloc(sizeof(double));
			memset(measurement_timestamp_usec, 0, sizeof(double));
			g_hash_table_insert(iface->measure_dict, "rate_timestamp_usec", measurement_timestamp_usec);
			*measurement_timestamp_usec = current_time.tv_usec;
			DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Logged new timestamp %f.%f\n",*measurement_timestamp_sec, *measurement_timestamp_usec);
		}
		else
		{
			*measurement_timestamp_usec = current_time.tv_usec;
			*measurement_timestamp_sec = current_time.tv_sec;
			DLOG(MAM_PMEASURE_THRUPUT_DEBUG,"Logged timestamp %f.%f\n",*measurement_timestamp_sec, *measurement_timestamp_usec);
		}
    }
	#endif
return;
}


/** Print the available measurement data for each prefix */
void pmeasure_print_prefix_summary(void *pfx, void *data)
{
	struct src_prefix_list *prefix = pfx;

	if (prefix == NULL || prefix->measure_dict == NULL)
		return;
    printf("Summary for prefix on interface %s, Family: %s\n", prefix->if_name, prefix->family == AF_INET?"IPv4":"IPv6");
	double *meanvalue = g_hash_table_lookup(prefix->measure_dict, "srtt_mean");
	if (meanvalue != NULL)
		printf("\tMean SRTT: %f ms\n", *meanvalue);

	double *medianvalue = g_hash_table_lookup(prefix->measure_dict, "srtt_median");
	if (medianvalue != NULL)
		printf("\tMedian SRTT: %f ms\n", *medianvalue);


    uint64_t  *rx_errors = g_hash_table_lookup(prefix->measure_dict, "rx_errors");
    if (rx_errors != NULL)
        printf("\tRX Errors: %" PRIu64 " \n", *rx_errors);

    uint64_t *tx_errors = g_hash_table_lookup(prefix->measure_dict, "tx_errors");
    if (medianvalue != NULL)
        printf("\tTX Errors: %" PRIu64 " \n", *tx_errors);

	printf("\n");
}

void pmeasure_print_iface_summary(void *ifc, void *data)
{
	struct iface_list *iface = ifc;

	if (iface == NULL || iface->measure_dict == NULL)
		return;

    printf("Summary for interface %s\n", iface->if_name);

    uint16_t *numsta = g_hash_table_lookup(iface->measure_dict, "number_of_stations");
	if (numsta != NULL)
        printf("\tNumber of stations: %" PRIu16 " \n", *numsta);

    uint8_t *chanutil = g_hash_table_lookup(iface->measure_dict, "channel_utilization_/255");
	if (chanutil != NULL)
        printf("\tChannel utilization: %" PRIu8 "/255 (%.2f%%)  \n", *chanutil, (*chanutil/255.0));

    uint16_t *adcap = g_hash_table_lookup(iface->measure_dict, "available_admission_capacity");
	if (adcap != NULL)
        printf("\tAvailable admission capacity: %" PRIu16 " \n", *adcap);

	printf("\n");
}

/** Log the available measurement data for each prefix, with timestamp and first prefix address
    Destination: MAM_PMEASURE_LOGPREFIX-prefix.log
 */
void pmeasure_log_prefix_summary(void *pfx, void *data)
{
    struct src_prefix_list *prefix = pfx;

    if (prefix == NULL || prefix->measure_dict == NULL)
        return;

    // Put together logfile name
    char *logfile;
    asprintf(&logfile, "%s-prefix.log", MAM_PMEASURE_LOGPREFIX);

    // Log timestamp
    if (data != NULL)
        _muacc_logtofile(logfile, "%d,", *(int *)data);
    else
        _muacc_logtofile(logfile, "NA,");

    // Construct string to print the first address of this prefix into
    char addr_str[INET6_ADDRSTRLEN+1];

    // Print first address of the prefix to the string, then print string to logfile
    if (prefix->family == AF_INET)
    {
        inet_ntop(AF_INET, &( ((struct sockaddr_in *) (prefix->if_addrs->addr))->sin_addr ), addr_str, sizeof(addr_str));
    }
    else if (prefix->family == AF_INET6)
    {
        inet_ntop(AF_INET6, &( ((struct sockaddr_in6 *) (prefix->if_addrs->addr))->sin6_addr ), addr_str, sizeof(addr_str));
    }
    _muacc_logtofile(logfile,"%s,", addr_str);

    // Log interface name that this prefix belongs to
    _muacc_logtofile(logfile, "%s,", prefix->if_name);

    double *meanvalue = g_hash_table_lookup(prefix->measure_dict, "srtt_mean");
    if (meanvalue != NULL)
        _muacc_logtofile(logfile, "%f,", *meanvalue);
    else
        _muacc_logtofile(logfile, "NA,");

    double *medianvalue = g_hash_table_lookup(prefix->measure_dict, "srtt_median");
    if (medianvalue != NULL)
        _muacc_logtofile(logfile, "%f,", *medianvalue);
    else
        _muacc_logtofile(logfile, "NA,");

	double *minimumvalue = g_hash_table_lookup(prefix->measure_dict, "srtt_minimum");
	if (minimumvalue != NULL)
		_muacc_logtofile(logfile, "%f,", *minimumvalue);
	else
		_muacc_logtofile(logfile, "NA,");

    uint64_t  *rx_errors = g_hash_table_lookup(prefix->measure_dict, "rx_errors");
    if (rx_errors != NULL)
        _muacc_logtofile(logfile, "%" PRIu64 ",", *rx_errors);
    else
        _muacc_logtofile(logfile, "NA,");

    uint64_t *tx_errors = g_hash_table_lookup(prefix->measure_dict, "tx_errors");
    if (medianvalue != NULL)
        _muacc_logtofile(logfile, "%" PRIu64 "\n", *tx_errors);
    else
        _muacc_logtofile(logfile, "NA\n");
}


/** Log the available measurement data for each interface, with timestamp
    Destination: MAM_PMEASURE_LOGPREFIX-interface.log
 */
void pmeasure_log_iface_summary(void *ifc, void *data)
{
    struct iface_list *iface = ifc;

    if (iface == NULL || iface->measure_dict == NULL)
        return;

    // Put together logfile name
    char *logfile;
    asprintf(&logfile, "%s-interface.log", MAM_PMEASURE_LOGPREFIX);

    // Log timestamp if available
    if (data != NULL)
        _muacc_logtofile(logfile, "%d,", *(int *)data);
    else
        _muacc_logtofile(logfile, "NA,");

	double *measurement_timestamp_sec = g_hash_table_lookup(iface->measure_dict,"rate_timestamp_sec");
	double *measurement_timestamp_usec = g_hash_table_lookup(iface->measure_dict,"rate_timestamp_usec");

	if (measurement_timestamp_sec != NULL && measurement_timestamp_usec != NULL)
		_muacc_logtofile(logfile, "%.0f.%.0f,", *measurement_timestamp_sec, *measurement_timestamp_usec);
	else
		_muacc_logtofile(logfile, "NA,");

	// Log interface name
	_muacc_logtofile(logfile, "%s,", iface->if_name);

    double *download_rate = g_hash_table_lookup(iface->measure_dict, "download_rate");
    if (download_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *download_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *download_max_rate = g_hash_table_lookup(iface->measure_dict, "download_max_rate");
    if (download_max_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *download_max_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *s_download_rate = g_hash_table_lookup(iface->measure_dict, "download_srate");
    if (s_download_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *s_download_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *download_max_srate = g_hash_table_lookup(iface->measure_dict, "download_max_srate");
    if (download_max_srate != NULL)
        _muacc_logtofile(logfile, "%f,", *download_max_srate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *upload_rate = g_hash_table_lookup(iface->measure_dict, "upload_rate");
    if (upload_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *upload_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *upload_max_rate = g_hash_table_lookup(iface->measure_dict, "upload_max_rate");
    if (upload_max_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *upload_max_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *s_upload_rate = g_hash_table_lookup(iface->measure_dict, "upload_srate");
    if (s_upload_rate != NULL)
        _muacc_logtofile(logfile, "%f,", *s_upload_rate);
    else
        _muacc_logtofile(logfile, "NA,");

    double *upload_max_srate = g_hash_table_lookup(iface->measure_dict, "upload_max_srate");
    if (upload_max_srate != NULL)
        _muacc_logtofile(logfile, "%f\n", *upload_max_srate);
    else
        _muacc_logtofile(logfile, "NA\n");
}

#ifdef HAVE_LIBNL
int create_nl_sock()
{
	int sock = 0;

	if ((sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_INET_DIAG)) ==-1)
	{
		perror("socket error");
		return EXIT_FAILURE;
	}
	return sock;
}

/*
 * Build and send a Netlink Request Message for the af - Family
 * on the given Socket.
 *
 * Returns the number of bytes sent, or -1 for errors.
 * */
int send_nl_msg(int sock, int af)
{
    // initialize structures
	struct msghdr msg;                 // Message structure
 	struct nlmsghdr nlh;               // Netlink Message Header
	struct sockaddr_nl sa;             // Socket address
	struct iovec iov[4];               // vector for information
	struct inet_diag_req_v2 request;   // Request structure
	int ret = 0;

	// set structures to 0
	memset(&msg, 0, sizeof(msg));
	memset(&sa, 0, sizeof(sa));
	memset(&nlh, 0, sizeof(nlh));
	memset(&request, 0, sizeof(request));

	// build the message
	sa.nl_family           = AF_NETLINK;
	request.sdiag_family   = af;
    request.sdiag_protocol = IPPROTO_TCP;

    // We're interested in all TCP Sockets except Sockets
    // in the states TCP_SYN_RECV, TCP_TIME_WAIT and TCP_CLOSE
    request.idiag_states = TCPF_ALL & ~((1<<TCP_SYN_RECV) | (1<<TCP_TIME_WAIT) | (1<<TCP_CLOSE));

    // Request tcp_info struct
    request.idiag_ext |= (1 << (INET_DIAG_INFO - 1));

    nlh.nlmsg_len = NLMSG_LENGTH(sizeof(request));

    // set message flags
    // note: NLM_F_MATCH is not working due to a bug,
    //       we have to do the filtering manual
    nlh.nlmsg_flags = NLM_F_MATCH | NLM_F_REQUEST;

    // Compose message
    nlh.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    iov[0].iov_base = (void*) &nlh;
    iov[0].iov_len = sizeof(nlh);
    iov[1].iov_base = (void*) &request;
    iov[1].iov_len = sizeof(request);

    msg.msg_name = (void*) &sa;
    msg.msg_namelen = sizeof(sa);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    //send the message
    ret = sendmsg(sock, &msg, 0);
    return ret;
}

/*
 * Receives Netlink Messages on the given Socket
 * and calls the parse_nl_msg() method to parse
 * the RTT values and add it to the values list
 *
 * Returns 0 on success and 1 on failure
 * */
int recv_nl_msg(int sock, void *pfx, GList **values)
{
    int numbytes = 0, rtalen =0;
    struct nlmsghdr *nlh;
    uint8_t msg_buf[BUFFER_SIZE];
    struct inet_diag_msg *diag_msg;

    while (1)
    {
        // receive the message
        numbytes = recv(sock, msg_buf, sizeof(msg_buf), 0);
        nlh = (struct nlmsghdr*) msg_buf;

        while (NLMSG_OK(nlh, numbytes))
        {
            // received last message
            if (nlh->nlmsg_type == NLMSG_DONE)
                return EXIT_SUCCESS;

            // Error in message
            if (nlh->nlmsg_type == NLMSG_ERROR)
            {
                DLOG(MAM_PMEASURE_NOISY_DEBUG1,"Error in netlink Message");
                return EXIT_FAILURE;
            }

            diag_msg = (struct inet_diag_msg*) NLMSG_DATA(nlh);
            // Attributes
            rtalen = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*diag_msg));

            // parse the message
            *values = parse_nl_msg(diag_msg, rtalen, pfx, *values);

            // get the next message
            nlh = NLMSG_NEXT(nlh, numbytes);
        }
    }
    return EXIT_SUCCESS;
}

/*
 * Parses a Netlink Message and add the RTT values to the values list
 *
 * */
GList * parse_nl_msg(struct inet_diag_msg *msg, int rtalen, void *pfx, GList *values)
{

    // structure for attributes
    struct rtattr *attr;
    struct tcp_info *tcpInfo;
    // sockaddr structure for prefix sockets
    struct sockaddr_in msg_addr_v4;
    struct sockaddr_in6 msg_addr_v6;
    
    char address[INET6_ADDRSTRLEN];
    memset(&address, 0, sizeof(address));

    if(msg->idiag_family == AF_INET)
    {
        msg_addr_v4.sin_family = msg->idiag_family;
        msg_addr_v4.sin_port = msg->id.idiag_sport;
        inet_ntop(AF_INET, &(msg->id.idiag_src), address, INET_ADDRSTRLEN);
        inet_pton(AF_INET, address, &(msg_addr_v4.sin_addr));

    } else if(msg->idiag_family == AF_INET6)
    {
        msg_addr_v6.sin6_family = AF_INET6;
        msg_addr_v6.sin6_port = msg->id.idiag_sport;
        inet_ntop(AF_INET6, (struct in_addr6 *) &(msg->id.idiag_src), address, INET6_ADDRSTRLEN);
        inet_pton(AF_INET6, address,  &(msg_addr_v6.sin6_addr));
    }

    // Find the right Socket
    switch(msg->idiag_family)
    {
        case(AF_INET6):
        {
            if ( (is_addr_in_pfx(pfx, &msg_addr_v6) != 0))
                return values; break;
        }
        case(AF_INET):
        {
            if ( (is_addr_in_pfx(pfx, &msg_addr_v4) != 0))
                return values; break;
        }
        default: return values;
    }
    //DLOG(MAM_PMEASURE_NOISY_DEBUG1,"%s is in the Prefixlist\n", address);

    // Get Attributes
    if (rtalen > 0)
    {
        attr = (struct rtattr*) (msg+1);

        while (RTA_OK(attr, rtalen))
        {
            if (attr->rta_type == INET_DIAG_INFO)
            {
                // Get rtt values
                tcpInfo = (struct tcp_info*) RTA_DATA(attr);
                double *rtt = malloc(sizeof(double));
                *rtt = tcpInfo->tcpi_rtt/1000.;

                // append it to the list of values
                values = g_list_append(values, rtt);
                //DLOG(MAM_PMEASURE_NOISY_DEBUG1, "Adding %f to values\n", *rtt);
            }
            //Get next attributes
            attr = RTA_NEXT(attr, rtalen);
        }
        }
    return values;
}
#endif

/** Compute the SRTT on an prefix, except on lo
 *  Insert it into the measure_dict as "srtt_median"
 */
void compute_srtt(void *pfx, void *data)
{
	struct src_prefix_list *prefix = pfx;

    // List for rtt values
    GList *values = NULL;

	if (prefix == NULL || prefix->measure_dict == NULL)
		return;


	if (prefix->if_name != NULL && strcmp(prefix->if_name,"lo"))
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Computing median SRTTs for a prefix of interface %s:\n", prefix->if_name);

		#ifdef HAVE_LIBNL
        // create the socket
        int sock_ip4 = create_nl_sock();
        int sock_ip6 = create_nl_sock();

        if (sock_ip4 == EXIT_FAILURE || sock_ip6 == EXIT_FAILURE)
            DLOG(MAM_PMEASURE_NOISY_DEBUG1, "Socket creation failed");

        // Create and send netlink messages
        // we have to send two different requests, the first time
        // with the IPv4 Flag and the other time with the IPv6 flag
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Sending IPv4 Request\n");
        if (send_nl_msg(sock_ip4, AF_INET) == -1)
            DLOG(MAM_PMEASURE_NOISY_DEBUG1, " Error sending Netlink Request");

        // receive messages
        if (recv_nl_msg(sock_ip4, prefix, &values) != 0)
            DLOG(MAM_PMEASURE_NOISY_DEBUG1, "Error receiving Netlink Messages")

        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Sending IPv6 Request\n");
        if (send_nl_msg(sock_ip6, AF_INET6) == -1)
            DLOG(MAM_PMEASURE_NOISY_DEBUG1, " Error sending Netlink Request");

        if (recv_nl_msg(sock_ip6, prefix, &values) != 0)
            DLOG(MAM_PMEASURE_NOISY_DEBUG1, "Error receiving Netlink Messages");

        // compute mean, median and minimum out of the
        // rtt values and write it into the dict
        compute_mean(prefix->measure_dict, values);
        compute_median(prefix->measure_dict, values);
        compute_minimum(prefix->measure_dict, values);

        // clean up
        g_list_free(values);
        close(sock_ip4);
        close(sock_ip6);
		#endif
    }
	return;
}

#ifdef HAVE_LIBNL
/*
 * Get TCP Statistics like TX_ERRORS and RX_ERRORS
 * of an Interface and insert it into the measure_dict
 *
 * */
void get_stats(void *pfx, void *data)
{
    struct nl_sock *sock;
    struct nl_cache *cache;
    struct rtnl_link *link;

    struct src_prefix_list *prefix = pfx;

    if (prefix == NULL || prefix->measure_dict == NULL)
        return;

    // Allocate Socket
    sock = nl_socket_alloc();

    if (!sock)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Error creating Socket");
        return;
    }
    // connect Socket
    if(nl_connect(sock, NETLINK_ROUTE) < 0)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Error connecting Socket");
        return;
    }
    // Allocate Link Cache
    if (rtnl_link_alloc_cache(sock, AF_UNSPEC, &cache) <0)
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Error allocating Link cache");
        nl_socket_free(sock);
        return;
    }
    // Get Interface by name
    if (!(link = rtnl_link_get_by_name(cache, prefix->if_name)))
    {
        DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Error getting Interface");
        return;
    }

    insert_errors(prefix->measure_dict, link);

    // clean up
    rtnl_link_put(link);
    nl_cache_put(cache);
    nl_socket_free(sock);
}
#endif

void pmeasure_setup(mam_context_t *ctx)
{
	DLOG(MAM_PMEASURE_NOISY_DEBUG0, "Setting up pmeasure \n");

	// Invoke callback explicitly to initialize stats
	pmeasure_callback(0, 0, ctx);
}

void pmeasure_cleanup(mam_context_t *ctx)
{
	DLOG(MAM_PMEASURE_NOISY_DEBUG0, "Cleaning up\n");
}

void pmeasure_callback(evutil_socket_t fd, short what, void *arg)
{
	mam_context_t *ctx = (mam_context_t *) arg;

	DLOG(MAM_PMEASURE_NOISY_DEBUG0, "Callback invoked.\n");

	if (ctx == NULL)
		return;

	g_slist_foreach(ctx->prefixes, &compute_srtt, NULL);
    g_slist_foreach(ctx->prefixes, &get_stats, NULL);

    DLOG(MAM_PMEASURE_NOISY_DEBUG2, "Computing Link Usage\n");
    g_slist_foreach(ctx->ifaces, &compute_link_usage, NULL);

	if (MAM_PMEASURE_NOISY_DEBUG2)
	{
		DLOG(MAM_PMEASURE_NOISY_DEBUG0, "Printing summary\n");
		g_slist_foreach(ctx->prefixes, &pmeasure_print_prefix_summary, NULL);
		g_slist_foreach(ctx->ifaces, &pmeasure_print_iface_summary, NULL);
	}
	if (MAM_PMEASURE_LOGPREFIX)
	{
		int timestamp = (int)time(NULL);
		g_slist_foreach(ctx->prefixes, &pmeasure_log_prefix_summary, &timestamp);
		g_slist_foreach(ctx->ifaces, &pmeasure_log_iface_summary, &timestamp);
	}

	DLOG(MAM_PMEASURE_NOISY_DEBUG0, "Callback finished.\n\n");
	DLOG(MAM_PMEASURE_NOISY_DEBUG2, "\n\n");
}
