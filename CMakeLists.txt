project(ast_redis)
cmake_minimum_required(VERSION 2.8)

#--------------------------------------------------
# Compiler Options
#--------------------------------------------------
SET(CMAKE_BUILD_TYPE "Debug")
#SET(CMAKE_C_FLAGS_DEBUG "-std=gnu11 -g -O0 -Wall -Werror")
#SET(CMAKE_C_FLAGS_RELEASE "-std=gnu11-O2 -Wall")
SET(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Werror")
SET(CMAKE_C_FLAGS_RELEASE "-O2 -Wall")
find_package(PkgConfig REQUIRED)

#--------------------------------------------------
# PThread is required
#-------------------------------------------------- 
find_package (Threads)

#--------------------------------------------------
# HiRedis is required
#-------------------------------------------------- 
pkg_check_modules (HIREDIS REQUIRED hiredis>=0.11.0)

#--------------------------------------------------
# LibEvent is required

#-------------------------------------------------- 
pkg_check_modules (LIBEVENT REQUIRED libevent)

#--------------------------------------------------
# Asterisk is required
#-------------------------------------------------- 
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules")
FIND_PACKAGE(Asterisk REQUIRED)

#--------------------------------------------------
# Compile Sources
#--------------------------------------------------
INCLUDE_DIRECTORIES(/usr/include/ ${PROJECT_BINARY_DIR})

# Config files

add_library(cdr_redis SHARED
	cdr_redis/cdr_redis.c
)
set_target_properties(cdr_redis PROPERTIES PREFIX "")
add_library(res_redis SHARED
#	include/shared.h
#	include/message_queue_pubsub.h
#	include/pbx_event_message_serializer.h
	lib/msq_redis.c
	@PBX_EVENT_SERIALIZER@
	res_redis/res_redis.c
)
set_target_properties(res_redis PROPERTIES PREFIX "")
add_library(res_config_redis SHARED
	res_config_redis/res_config_redis.c
)
set_target_properties(res_config_redis PROPERTIES PREFIX "")
target_link_libraries(res_config_redis -l@HIREDIS_LIBRARIES@ ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(res_redis -l@HIREDIS_LIBRARIES@ -l@LIBEVENT_LIBRARIES@ ${CMAKE_THREAD_LIBS_INIT})

#--------------------------------------------------
# Install
#--------------------------------------------------
install(TARGETS cdr_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(TARGETS res_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(TARGETS res_config_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(FILES conf/res_redis.conf DESTINATION asterisk-11-branch/etc/asterisk/ COMPONENT config)
