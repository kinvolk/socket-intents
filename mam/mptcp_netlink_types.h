/** \file mptcp_mam_netlink.h
 *	Netlink helpers / family data structures
 */
#ifndef __MPTCP_NETLINK_TYPES_H__
#define __MPTCP_NETLINK_TYPES_H__

/* commands */
enum
{
	MAM_MPTCP_C_UNSPEC=0,
    MAM_MPTCP_C_INIT,
    MAM_MPTCP_C_NEWFLOW,
    MAM_MPTCP_C_NEWIFACE,
    //TODO implement
    //MAM_MPTCP_C_REMIFACE,
    //MAM_MPTCP_C_INFOMSG,
    __MAM_MPTCP_C_MAX
};
#define MAM_MPTCP_C_MAX (__MAM_MPTCP_C_MAX - 1)

/* attributes */
enum
{
    MAM_MPTCP_A_UNSPEC=0,
    MAM_MPTCP_A_STRMSG,
   
    MAM_MPTCP_A_IPV4,
    MAM_MPTCP_A_IPV4_LOC,
    MAM_MPTCP_A_IPV4_LOC_ID,
    MAM_MPTCP_A_IPV4_LOC_PRIO,
   
    MAM_MPTCP_A_IPV4_REM,
    MAM_MPTCP_A_IPV4_REM_ID,
    MAM_MPTCP_A_IPV4_REM_PRIO,
    MAM_MPTCP_A_IPV4_REM_BIT,
    MAM_MPTCP_A_IPV4_REM_RETR_BIT,
    MAM_MPTCP_A_IPV4_REM_PORT,
   
    MAM_MPTCP_A_INODE,
    MAM_MPTCP_A_IPV6,
    MAM_MPTCP_A_OK,
    MAM_MPTCP_A_NOT_OK,
    MAM_MPTCP_A_TOKEN,
    __MAM_MPTCP_A_MAX
};
#define MAM_MPTCP_A_MAX (__MAM_MPTCP_A_MAX - 1)

enum{
	MAM_MPTCP_N_A_IPV6_0=0,
	MAM_MPTCP_N_A_IPV6_1,
	MAM_MPTCP_N_A_IPV6_2,
	MAM_MPTCP_N_A_IPV6_3,
	__MAM_MPTCP_N_A_MAX
};
#define MAM_MPTCP_N_A_MAX (__MAM_MPTCP_N_A_MAX - 1)

#define MAM_MPTCP_A_STRMSG_MAXLEN 1024

#endif /* __MPTCP_NETLINK_TYPES_H__ */
