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

#
# CDR_REDIS
#
add_library(cdr_redis SHARED
	cdr_redis/cdr_redis.c
)
set_target_properties(cdr_redis PROPERTIES PREFIX "")

#
# RES_CONFIG_REDIS
#
add_library(res_config_redis SHARED
	res_config_redis/res_config_redis.c
)

#
# RES_REDIS
#
add_library(res_redis SHARED
#	include/shared.h
#	include/message_queue_pubsub.h
#	include/pbx_event_message_serializer.h
	lib/msq_redis.c
	@PBX_EVENT_SERIALIZER@
	res_redis/res_redis.c
)
set_target_properties(res_redis PROPERTIES PREFIX "")

#
# RES_REDIS_v1
#
add_library(res_redis_v1 SHARED
	include/shared.h
	include/message_queue_pubsub.h
	include/pbx_event_message_serializer.h
	lib/msq_redis.c
	@PBX_EVENT_SERIALIZER@
	res_redis/res_redis_v1.c
)
set_target_properties(res_redis_v1 PROPERTIES PREFIX "")

set_target_properties(res_config_redis PROPERTIES PREFIX "")
target_link_libraries(res_config_redis -l@HIREDIS_LIBRARIES@ ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(res_redis -l@HIREDIS_LIBRARIES@ -l@LIBEVENT_LIBRARIES@ ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(res_redis_v1 -l@HIREDIS_LIBRARIES@ -l@LIBEVENT_LIBRARIES@ ${CMAKE_THREAD_LIBS_INIT})

#--------------------------------------------------
# Install
#--------------------------------------------------
install(TARGETS cdr_redis DESTINATION ${ASTERISK_MOD_DIR})
install(TARGETS res_redis DESTINATION ${ASTERISK_MOD_DIR})
install(TARGETS res_redis_v1 DESTINATION ${ASTERISK_MOD_DIR})
install(TARGETS res_config_redis DESTINATION ${ASTERISK_MOD_DIR})
install(FILES conf/res_redis.conf DESTINATION ${ASTERISK_ETC_DIR}/ COMPONENT config)
install(FILES conf/res_redis_v1.conf DESTINATION ${ASTERISK_ETC_DIR}/ COMPONENT config)
