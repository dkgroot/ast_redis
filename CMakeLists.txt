project(ast_redis)
cmake_minimum_required(VERSION 2.8)

#--------------------------------------------------
# Compiler Options
#--------------------------------------------------
SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Werror")
SET(CMAKE_C_FLAGS_RELEASE "-O2 -Wall")

#--------------------------------------------------
# HiRedis is required
#-------------------------------------------------- 
find_package(PkgConfig REQUIRED)
pkg_check_modules (HIREDIS REQUIRED hiredis)
#INCLUDE_DIRECTORIES(${HIREDIS_INCLUDE_DIR})
#LINK_DIRECTORIES(${HIREDIS_LIBRARYDIR})

#--------------------------------------------------
# LibEvent is required
#-------------------------------------------------- 
pkg_check_modules (LIBEVENT REQUIRED libevent)

#--------------------------------------------------
# Asterisk is required
#-------------------------------------------------- 
SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/CMakeModules")
FIND_PACKAGE(Asterisk REQUIRED)

INCLUDE_DIRECTORIES(/usr/include/ ${PROJECT_BINARY_DIR})

#CONFIGURE_FILE ("${PROJECT_SOURCE_DIR}/include/config.h.in" "${PROJECT_BINARY_DIR}/config.h" )
#--------------------------------------------------
# Compile Sources
#--------------------------------------------------

# Config files

# Static library location for now (Needs Fixing)....
add_library(hiredis STATIC IMPORTED)
set_property(TARGET hiredis PROPERTY IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libhiredis.so)
add_library(pthread STATIC IMPORTED)
set_property(TARGET pthread PROPERTY IMPORTED_LOCATION /lib/x86_64-linux-gnu/libpthread.so.0)
add_library(libevent STATIC IMPORTED)
set_property(TARGET libevent PROPERTY IMPORTED_LOCATION /usr/lib/x86_64-linux-gnu/libevent.so)

add_library(cdr_redis SHARED
	cdr_redis/cdr_redis.c
)
set_target_properties(cdr_redis PROPERTIES PREFIX "")
add_library(res_redis SHARED
	res_redis/res_redis.c
	include/ast_event_json.h
	lib/ast_event_json.c
)
set_target_properties(res_redis PROPERTIES PREFIX "")
add_library(res_config_redis SHARED
	res_config_redis/res_config_redis.c
)
set_target_properties(res_config_redis PROPERTIES PREFIX "")
target_link_libraries(res_config_redis hiredis pthread)
target_link_libraries(res_redis hiredis pthread libevent)

#--------------------------------------------------
# Install
#--------------------------------------------------
install(TARGETS cdr_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(TARGETS res_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(TARGETS res_config_redis DESTINATION ${ASTERISK_LIBRARY_DIR})
install(FILES conf/res_redis.conf DESTINATION asterisk-11-branch/etc/asterisk/ COMPONENT config)
