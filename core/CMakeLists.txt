INCLUDE(${CMAKE_SOURCE_DIR}/cmake/audio.rules.txt)

FILE (GLOB sems_SRCS "*.cpp" "sip/*.cpp" "*.c")

SET(sems_common_SRCS ${sems_SRCS})
LIST(REMOVE_ITEM sems_common_SRCS ${CMAKE_SOURCE_DIR}/core/sems.cpp)

SET(aux_binaries decode_test)

set (audio_files
beep.wav
default_en.wav
codecs_bench.wav
)

INCLUDE_DIRECTORIES (ampi)
INCLUDE_DIRECTORIES (amci)

SET(common_LIBS stdc++ resolv)

ADD_EXECUTABLE (sems ${sems_SRCS})
FOREACH(aux_binary IN LISTS aux_binaries)
	ADD_EXECUTABLE (sems_${aux_binary} aux/${aux_binary}.cpp ${sems_common_SRCS})
ENDFOREACH(aux_binary)

IF(NOT MAX_RTP_SESSIONS)
	SET(MAX_RTP_SESSIONS 2048)
ENDIF(NOT MAX_RTP_SESSIONS)

#ADD_DEFINITIONS(-DMAX_RTP_SESSIONS=${MAX_RTP_SESSIONS})
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMAX_RTP_SESSIONS=${MAX_RTP_SESSIONS}")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DMAX_RTP_SESSIONS=${MAX_RTP_SESSIONS}")


IF(SPANDSP_FOUND)
	LIST(APPEND common_LIBS spandsp)
ENDIF(SPANDSP_FOUND)

IF(LIBSAMPLERATE_FOUND)
	LIST(APPEND common_LIBS samplerate)
ENDIF(LIBSAMPLERATE_FOUND)

IF(LIBZRTP_FOUND)
	LIST(APPEND common_LIBS zrtp)
ENDIF(LIBZRTP_FOUND)

TARGET_LINK_LIBRARIES(sems ${CMAKE_DL_LIBS} ${common_LIBS})
FOREACH(aux_binary IN LISTS aux_binaries)
	TARGET_LINK_LIBRARIES(sems_${aux_binary} ${CMAKE_DL_LIBS} ${common_LIBS})
ENDFOREACH(aux_binary)

ADD_SUBDIRECTORY(plug-in)

# Create config-file from template
CONFIGURE_FILE (${CMAKE_SOURCE_DIR}/core/etc/app_mapping.conf.sample ${CMAKE_SOURCE_DIR}/core/etc/app_mapping.conf)
CONFIGURE_FILE (${CMAKE_SOURCE_DIR}/core/etc/sems.conf.cmake ${CMAKE_SOURCE_DIR}/core/etc/sems.conf.dist)

# Installation of main SEMS executable
INSTALL(TARGETS sems RUNTIME DESTINATION ${SEMS_EXEC_PREFIX}/sbin)
FOREACH(aux_binary IN LISTS aux_binaries)
	INSTALL(TARGETS sems_${aux_binary} RUNTIME DESTINATION ${SEMS_EXEC_PREFIX}/sbin)
ENDFOREACH(aux_binary)


# Installation of config-files
INSTALL(FILES ./etc/sems.conf.dist DESTINATION ${SEMS_CFG_PREFIX}/etc/sems/)
INSTALL(FILES ./etc/app_mapping.conf DESTINATION ${SEMS_CFG_PREFIX}/etc/sems/etc/)
IF(LIBZRTP_FOUND)
INSTALL(FILES ./etc/zrtp.conf DESTINATION ${SEMS_CFG_PREFIX}/etc/sems/etc/)
ENDIF(LIBZRTP_FOUND)

INCLUDE(${CMAKE_SOURCE_DIR}/cmake/doc.rules.txt)

SET(doc_dirs ../doc/figures)
SET(doc_files
	../doc/Applications.txt
	../doc/Howtostart_noproxy.txt
	../doc/Howtostart_simpleproxy.txt
	../doc/Howtostart_voicemail.txt
	../doc/CHANGELOG
	../doc/COPYING
	../doc/Tuning.txt
)

#SEMS_INSTALL_DOCS("${doc_files}" "${doc_dirs}")
SEMS_INSTALL_AUDIO("" "${audio_files}")
