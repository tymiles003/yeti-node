find_package(PostgreSQL REQUIRED)

set (yeti_SRCS
	yeti.cpp
        SqlCallProfile.cpp
        Cdr.cpp
	CdrList.cpp
	ProfilesCache.cpp
	PgConnectionPool.cpp
	CdrWriter.cpp
	SqlRouter.cpp
	SqlCallProfile.cpp
	DbConfig.cpp
)

SET(yeti_config_files
	etc/lm_yeti.cfg.dist
)

INCLUDE_DIRECTORIES(../..)	#relative path to SBC headers

#message ( FATAL_ERROR ${POSTGRESQL_LIBRARIES} )

SET(sems_module_libs -lpqxx)
SET(sems_module_name yeti)

INCLUDE(${CMAKE_SOURCE_DIR}/cmake/module.rules.txt)
