# Locate asterisk includes and library
# This module defines
# ASTERISK_LIBRARY_DIR, the name of the asterisk modules library
# ASTERISK_INCLUDE_DIR, where to find asterisk includes
#

find_package(PkgConfig)
pkg_check_modules (ASTERISK   asterisk)

if (ASTERISK_FOUND)
	set(ASTERISK_INCLUDE_DIR ${ASTERISK_INCLUDE_DIRS})
	set(ASTERISK_LIBRARY_DIR ${ASTERISK_LIBRARY_DIRS})
else (ASTERISK_FOUND)
	find_path(ASTERISK_INCLUDE_DIR asterisk.h
	HINTS
	  PATH_SUFFIXES include
	PATHS
	  /usr/incude/asterisk
	  /usr/include
	  ~/Library/Frameworks
	  /Library/Frameworks
	  /usr/local/asterisk*
	  /usr/local
	  /usr
	  /sw
	  /opt/asterisk*
	  /opt/local
	  /opt/csw
	  /opt
	  /mingw
	  /usr/local/asterisk-11-branch
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
	  /usr/local/asterisk-11-branch/lib/asterisk
	)
endif(ASTERISK_FOUND)

IF(NOT ASTERISK_INCLUDE_DIR)
	MESSAGE(FATAL_ERROR "Build will fail, asterisk was not found")
	RETURN()
ENDIF(NOT ASTERISK_INCLUDE_DIR)

MESSAGE("ASTERISK_INCLUDE_DIR is ${ASTERISK_INCLUDE_DIR}")
MESSAGE("ASTERISK_LIBRARY_DIR is ${ASTERISK_LIBRARY_DIR}")

# Register directories
INCLUDE_DIRECTORIES(${ASTERISK_INCLUDE_DIR})
LINK_DIRECTORIES(${ASTERISK_LIBRARY_DIR})
set(CMAKE_REQUIRED_INCLUDES ${ASTERISK_INCLUDE_DIR})

# Find Header Files
INCLUDE (CheckIncludeFiles)
CHECK_INCLUDE_FILES ("asterisk/ast_version.h" HAVE_PBX_AST_VERSION_H)
if(NOT HAVE_PBX_AST_VERSION_H)
	CHECK_INCLUDE_FILES ("asterisk/version.h" HAVE_PBX_VERSION_H)
	if (NOT HAVE_PBX_VERSION_H)
		MESSAGE(FATAL_ERROR "Build will fail, asterisk version was not found")
		RETURN()
	endif()
endif()
CHECK_INCLUDE_FILES ("asterisk/lock.h" HAVE_PBX_LOCK_H)
CHECK_INCLUDE_FILES ("asterisk/acl.h" HAVE_PBX_ACL_H)
CHECK_INCLUDE_FILES ("asterisk/buildopts.h" HAVE_PBX_BUILDOPTS_H)
CHECK_INCLUDE_FILES ("asterisk/abstract_jb.h" HAVE_PBX_ABSTRACT_JB_H)
CHECK_INCLUDE_FILES ("asterisk/app.h" HAVE_PBX_APP_H)
#CHECK_INCLUDE_FILES ("asterisk/.h" HAVE_PBX__H)

#
# Or
#
find_file(ast_version asterisk/ast_version.h PATHS ${ASTERISK_INCLUDE_DIR} NO_DEFAULT_PATH)
if(NOT ast_version)
        find_file(version asterisk/version.h PATHS ${ASTERISK_INCLUDE_DIR} NO_DEFAULT_PATH)
        if (NOT version)
                MESSAGE(FATAL_ERROR "Build will fail, asterisk version was not found")
                RETURN()
        endif()
endif()
