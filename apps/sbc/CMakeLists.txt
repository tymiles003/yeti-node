find_package(PQXX REQUIRED)

ADD_SUBDIRECTORY(logic_control/yeti)

set (sbc_SRCS
HeaderFilter.cpp
ParamReplacer.cpp
RTPParameters.cpp
RegexMapper.cpp
SBC.cpp
SBCCallProfile.cpp
SDPFilter.cpp
RegisterCache.cpp
SubscriptionDialog.cpp
SBCSimpleRelay.cpp
RegisterDialog.cpp
arg_conversion.cpp
SBCEventLog.cpp
SBCCallLeg.cpp
CallLeg.cpp
RateLimit.cpp
SessionUpdate.cpp
ReplacesMapper.cpp
SBCCallRegistry.cpp
)

SET(sbc_config_files
#auth_b2b.sbcprofile
#call_timer.sbcprofile
#codecfilter.sbcprofile
#prepaid.sbcprofile
#refuse.sbcprofile
#replytranslate.sbcprofile
sbc
#src_ipmap
#sst_b2b.sbcprofile
#transparent.sbcprofile
#symmetricrtp.sbcprofile
)

INSTALL(PROGRAMS
#tools/sems-sbc-get-activeprofile
tools/sems-sbc-get-regex-map-names
#tools/sems-sbc-list-profiles
#tools/sems-sbc-load-profile
#tools/sems-sbc-reload-profile
#tools/sems-sbc-reload-profiles
#tools/sems-sbc-set-activeprofile
tools/sems-sbc-set-regex-map
	DESTINATION ${SEMS_EXEC_PREFIX}/sbin
	)

INCLUDE_DIRECTORIES(${PQXX_INCLUDE_DIRECTORIES})
SET(sems_module_libs ${PQXX_LIBRARIES})

SET(sems_module_name sbc)
INCLUDE(${CMAKE_SOURCE_DIR}/cmake/module.rules.txt)

