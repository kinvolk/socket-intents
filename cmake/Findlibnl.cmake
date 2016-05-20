# - Find libnl
#
# This module defines
#  LIBNL_LIBRARIES - the libnl libraries
#  LIBNL_INCLUDE_DIR - the include path of the libnl and libgenl libraries

find_library (LIBNL_LIBRARY nl-3)
find_library (LIBNL_GENERIC_LIBRARY nl-genl-3)

if ( ${LIBNL_LIBRARY} MATCHES "LIBNL_LIBRARY-NOTFOUND" OR  ${LIBNL_GENERIC_LIBRARY} MATCHES "LIBNL_GENERIC_LIBRARY-NOTFOUND" )
	message( STATUS "Compiling without libnl - Library not Found." )
	SET (LIBNL_GENERIC_LIBRARY "")
	SET (LIBNL_INCLUDE_DIR "")
	SET (LIBNL_LIBRARY "")
	SET (NETLINK_CODE_FILES "")
else ()
	message ( STATUS "Found Libnl and Libnl-genl")
	SET (NETLINK_CODE_FILES "mam_netlink.c" "mptcp_netlink_parser.c")
	SET (HAVE_LIBNL 1)
	add_definitions( -DHAVE_LIBNL )
endif ()

set(LIBNL_LIBRARIES 
	${LIBNL_LIBRARY} 
	${LIBNL_GENERIC_LIBRARY}
)

find_path (LIBNL_INCLUDE_DIR
  NAMES
  netlink/netlink.h
  PATH_SUFFIXES
  libnl3
)
