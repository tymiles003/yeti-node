set (yeti_SRCS
	yeti.cpp
        SqlCallProfile.cpp
        Cdr.cpp
)

SET(yeti_config_files
	etc/lm_yeti.cfg.dist
)

#INSTALL(TARGETS SharedLibraryTarget
#	lm_yeti.so
#	DESTINATION ${SEMS_LIB_PREFIX}/
#	 LIBRARY DESTINATION lib
#)

INCLUDE_DIRECTORIES(../..)	#realative path to SBC headers

SET(sems_module_name yeti)
INCLUDE(${CMAKE_SOURCE_DIR}/cmake/module.rules.txt)
