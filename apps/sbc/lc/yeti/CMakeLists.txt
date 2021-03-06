find_package(PQXX REQUIRED)
find_package(Hiredis REQUIRED)

set (yeti_SRCS
	yeti.cpp
	yeti_cc.cpp
	yeti_xmlrpc.cpp
	alarms.cpp
	SqlRouter.cpp
	SqlCallProfile.cpp
	CallCtx.cpp
	CodesTranslator.cpp
	Sensors.cpp
	Registration.cpp
	UsedHeaderField.cpp
	CodecsGroup.cpp
	sdp_filter.cpp
	codecs_bench.cpp
	dtmf_sip_info.cpp
	resources/RedisConnPool.cpp
        resources/ResourceCache.cpp
        resources/ResourceControl.cpp
	resources/Resource.cpp
	cdr/Cdr.cpp
	cdr/CdrWriter.cpp
	cdr/TrustedHeaders.cpp
	hash/CdrList.cpp
	hash/ProfilesCache.cpp
	db/PgConnectionPool.cpp
	db/DbConfig.cpp
)

SET(yeti_config_files
	yeti.dist
	oodprofile.yeti.dist
)

#INSTALL(FILES
#	etc/yeticmd.yaml.dist
#	DESTINATION ${SEMS_CFG_PREFIX}/etc/sems/etc/
#)

INSTALL(PROGRAMS
	tools/yetictl
#	tools/yeticmd
	tools/yeti-getcounters
	tools/yeti-sumcounters
	tools/yeti-csv2sql
	tools/yeti_core_log.gdb
	tools/yeti-corelog
	tools/yeti-remove-obsolete-dumps
	DESTINATION ${SEMS_EXEC_PREFIX}/sbin
)

INCLUDE_DIRECTORIES(../..)	#relative path to SBC headers
INCLUDE_DIRECTORIES(${HIREDIS_INCLUDE_DIR} ${PQXX_INCLUDE_DIRECTORIES})
INCLUDE_DIRECTORIES(${CMAKE_SOURCE_DIR}/apps/xmlrpc2di/xmlrpc++/src)

SET(sems_module_libs ${HIREDIS_LIBRARIES} ${PQXX_LIBRARIES})
SET(sems_module_name yeti)

IF(EXISTS ${CMAKE_SOURCE_DIR}/.git)
  FIND_PACKAGE(Git)
  IF(GIT_FOUND)
    EXECUTE_PROCESS(  COMMAND ${GIT_EXECUTABLE} rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE "COMMIT_HASH" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    EXECUTE_PROCESS(  COMMAND ${GIT_EXECUTABLE} describe HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE "GIT_DESCRIBE" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    SET(BUILD_VERSION ${GIT_DESCRIBE})
    SET(BUILD_COMMIT ${COMMIT_HASH})
  ELSE(GIT_FOUND)
    SET(BUILD_VERSION 0)
  ENDIF(GIT_FOUND)
ENDIF(EXISTS ${CMAKE_SOURCE_DIR}/.git)

EXECUTE_PROCESS( COMMAND "date" OUTPUT_VARIABLE "BUILD_DATE" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE) 
EXECUTE_PROCESS( COMMAND "whoami" OUTPUT_VARIABLE "BUILD_USER" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE) 

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/Version.h @ONLY)

INCLUDE(${CMAKE_SOURCE_DIR}/cmake/module.rules.txt)
