# CMake toolchain file for cross-compiling to MS-DOS with DJGPP.
#
#   cmake -S . -B build-dos -DCMAKE_TOOLCHAIN_FILE=cmake/djgpp.cmake \
#         -DALLEGRO4_DIR=$HOME/tools/src/allegro-4.2
#
# Set DJGPP_PREFIX (environment or cache) if DJGPP is not in ~/djgpp.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR i586)

if(NOT DJGPP_PREFIX)
	if(DEFINED ENV{DJGPP_PREFIX})
		set(DJGPP_PREFIX "$ENV{DJGPP_PREFIX}")
	else()
		set(DJGPP_PREFIX "$ENV{HOME}/djgpp")
	endif()
endif()
set(DJGPP_PREFIX "${DJGPP_PREFIX}" CACHE PATH "DJGPP installation prefix")

set(DJGPP_TARGET i586-pc-msdosdjgpp)

set(CMAKE_C_COMPILER   "${DJGPP_PREFIX}/bin/${DJGPP_TARGET}-gcc")
set(CMAKE_CXX_COMPILER "${DJGPP_PREFIX}/bin/${DJGPP_TARGET}-g++")
set(CMAKE_AR           "${DJGPP_PREFIX}/bin/${DJGPP_TARGET}-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${DJGPP_PREFIX}/bin/${DJGPP_TARGET}-ranlib" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH "${DJGPP_PREFIX}/${DJGPP_TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_EXECUTABLE_SUFFIX ".exe")

set(MSDOS ON)
