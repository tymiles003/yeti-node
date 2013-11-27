find_package(PQXX REQUIRED)
find_package(Hiredis REQUIRED)

set (yeti_SRCS
	yeti.cpp
	CdrList.cpp
	ProfilesCache.cpp
	PgConnectionPool.cpp
	CdrWriter.cpp
	SqlRouter.cpp
	DbConfig.cpp
        RedisConnPool.cpp
        ResourceCache.cpp
        ResourceControl.cpp
	Cdr.cpp
	SqlCallProfile.cpp
	Resource.cpp
	CallCtx.cpp
	CodesTranslator.cpp
	Registration.cpp
	UsedHeaderField.cpp
)

SET(yeti_config_files
	yeti.dist
	oodprofile.yeti.dist
)

INSTALL(PROGRAMS
	tools/yetictl
	tools/yeti-version
	tools/yeti-stats
	tools/yeti-callscount
	tools/yeti-showconfig
	tools/yeti-getcounters
	tools/yeti-sumcounters
	tools/yeti-csv2sql
	tools/yeti-dumplog
	tools/yeti_core_log.gdb
	tools/yeti-corelog
	DESTINATION ${SEMS_EXEC_PREFIX}/sbin
)

INCLUDE_DIRECTORIES(../..)	#relative path to SBC headers
INCLUDE_DIRECTORIES(${HIREDIS_INCLUDE_DIR} ${PQXX_INCLUDE_DIRECTORIES})

SET(sems_module_libs ${HIREDIS_LIBRARIES} ${PQXX_LIBRARIES})
SET(sems_module_name yeti)

IF(EXISTS ${CMAKE_SOURCE_DIR}/.git)
  FIND_PACKAGE(Git)
  IF(GIT_FOUND)
    EXECUTE_PROCESS(  COMMAND ${GIT_EXECUTABLE} rev-parse HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE "BUILD_VERSION" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  ELSE(GIT_FOUND)
    SET(BUILD_VERSION 0)
  ENDIF(GIT_FOUND)
ENDIF(EXISTS ${CMAKE_SOURCE_DIR}/.git)

EXECUTE_PROCESS( COMMAND "date" OUTPUT_VARIABLE "BUILD_DATE" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE) 
EXECUTE_PROCESS( COMMAND "whoami" OUTPUT_VARIABLE "BUILD_USER" ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE) 

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/Version.h @ONLY)

INCLUDE(${CMAKE_SOURCE_DIR}/cmake/module.rules.txt)
