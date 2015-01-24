# Locate asterisk includes and library
# This module defines
# ASTERISK_LIBRARY_DIR, the name of the asterisk modules library
# ASTERISK_INCLUDE_DIR, where to find asterisk includes
#

#find_package(PkgConfig)
#pkg_check_modules (ASTERISK   asterisk)

if (ASTERISK_FOUND)
	set(ASTERISK_INCLUDE_DIR ${ASTERISK_INCLUDE_DIRS})
	set(ASTERISK_LIBRARY_DIR ${ASTERISK_LIBRARY_DIRS})
else (ASTERISK_FOUND)
	find_path(ASTERISK_INCLUDE_DIR asterisk.h
	HINTS
	  PATH_SUFFIXES 
	PATHS
	  /usr/include
	  ~/Library/Frameworks
	  /Library/Frameworks
	  /usr/local/asterisk*/include
	  /usr/local
	  /usr
	  /sw
	  /opt/asterisk*/include
	  /opt/local
	  /opt/csw
	  /opt
	  /mingw
	)
	find_path(ASTERISK_LIBRARY_DIR app_dial.so
	HINTS
	  PATH_SUFFIXES modules
	PATHS
	  /usr/lib/asterisk
	  /usr/lib64/asterisk
	  /usr/local/lib/asterisk
	  /usr/local/lib64/asterisk
	  /usr/local/asterisk*/lib/asterisk
	  /opt/asterisk*/lib/asterisk
	)
endif(ASTERISK_FOUND)

IF(NOT ASTERISK_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR "Build will fail, asterisk was not found")
	RETURN()
ENDIF(NOT ASTERISK_INCLUDE_DIR)

# Register directories
INCLUDE_DIRECTORIES(/usr/include ${ASTERISK_INCLUDE_DIR})
LINK_DIRECTORIES(${ASTERISK_LIBRARY_DIR})
set(CMAKE_REQUIRED_INCLUDES ${ASTERISK_INCLUDE_DIR})

# Find Header Files
INCLUDE (CheckIncludeFiles)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/module.h;asterisk/logger.h;asterisk/config.h" HAVE_PBX_ASTERISK_H)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/version.h" HAVE_PBX_VERSION_H)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/ast_version.h" HAVE_PBX_AST_VERSION_H)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/lock.h" HAVE_PBX_LOCK_H)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/event.h" HAVE_PBX_EVENT_H)
CHECK_INCLUDE_FILES("asterisk.h;asterisk/stasis.h" HAVE_PBX_STASIS_H)
if(NOT HAVE_PBX_STASIS_H)
	set(HAVE_PBX_VERSION_11 1)
	MESSAGE("Dealing with Asterisk <= Version 11")
endif()
# CHECK_TYPE_SIZE("int"   SIZEOF_INT)

if(NOT HAVE_PBX_AST_VERSION_H AND NOT HAVE_PBX_VERSION_H)
	MESSAGE(FATAL_ERROR "Build will fail, asterisk version was not found")
	RETURN()
endif()

MESSAGE("-- Set ASTERISK_INCLUDE_DIR = ${ASTERISK_INCLUDE_DIR}")
MESSAGE("-- Set ASTERISK_LIBRARY_DIR = ${ASTERISK_LIBRARY_DIR}")
CONFIGURE_FILE ("${PROJECT_SOURCE_DIR}/config.h.in" "${PROJECT_BINARY_DIR}/config.h" )
