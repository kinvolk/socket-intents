INCLUDE_DIRECTORIES(${GLIB2_INCLUDE_DIR})

ADD_LIBRARY(policy_sample MODULE policy_sample.c policy_util.c)
SET_TARGET_PROPERTIES(policy_sample PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_sample mam ${GLIB2_LIBRARIES})

ADD_LIBRARY(policy_rr_naive MODULE policy_rr_naive.c policy_util.c)
SET_TARGET_PROPERTIES(policy_rr_naive PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_rr_naive mam ${GLIB2_LIBRARIES})

ADD_LIBRARY(policy_filesize MODULE policy_filesize.c policy_util.c)
SET_TARGET_PROPERTIES(policy_filesize PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_filesize mam ${GLIB2_LIBRARIES})


if ( ${HAVE_LIBNL} )
ADD_LIBRARY(policy_mptcp_filesize MODULE policy_mptcp_filesize.c policy_util.c)
SET_TARGET_PROPERTIES(policy_mptcp_filesize PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_mptcp_filesize mam ${GLIB2_LIBRARIES})

INCLUDE_DIRECTORIES(${LIBNL_INCLUDE_DIR})
ADD_LIBRARY(policy_mptcp_default_flow MODULE policy_mptcp_default_flow.c policy_util.c ../mam/mam_netlink.c)
SET_TARGET_PROPERTIES(policy_mptcp_default_flow PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_mptcp_default_flow mam ${GLIB2_LIBRARIES} ${LIBNL_LIBRARIES})
endif ()

ADD_LIBRARY(policy_intents MODULE policy_intents.c policy_util.c)
SET_TARGET_PROPERTIES(policy_intents PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_intents mam ${GLIB2_LIBRARIES})

ADD_LIBRARY(policy_rr_pipelining MODULE policy_rr_pipelining.c policy_util.c)
SET_TARGET_PROPERTIES(policy_rr_pipelining PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_rr_pipelining mam ${GLIB2_LIBRARIES})

ADD_LIBRARY(policy_earliest_arrival MODULE policy_earliest_arrival.c policy_util.c)
SET_TARGET_PROPERTIES(policy_earliest_arrival PROPERTIES PREFIX "")
TARGET_LINK_LIBRARIES(policy_earliest_arrival mam ${GLIB2_LIBRARIES})

INSTALL(TARGETS policy_sample policy_rr_naive policy_filesize policy_intents policy_rr_pipelining policy_earliest_arrival
	DESTINATION "${CMAKE_INSTALL_PREFIX}/${POLICY_PATH}"
)

if (${HAVE_LIBNL} )
#MPTCP policies
INSTALL(TARGETS policy_mptcp_filesize policy_mptcp_default_flow
	DESTINATION "${CMAKE_INSTALL_PREFIX}/${POLICY_PATH}"
)
endif ()
