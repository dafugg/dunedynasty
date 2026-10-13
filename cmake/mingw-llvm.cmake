# CMake toolchain file for cross-compiling the Windows x64 build on Linux
# with llvm-mingw (UCRT) against MSYS2 UCRT64 libraries, the same ones
# upstream's Windows CI uses.  See scripts/windows/build-windows-x64-cross.sh.
#
#   LLVM_MINGW   llvm-mingw installation (~/tools/llvm-mingw)
#   MINGW_DEPS   directory holding ucrt64/ from fetch-msys2-deps.py
#                (~/tools/mingw-ucrt64)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

foreach(var LLVM_MINGW MINGW_DEPS)
	if(NOT ${var} AND DEFINED ENV{${var}})
		set(${var} "$ENV{${var}}")
	endif()
endforeach()
if(NOT LLVM_MINGW)
	set(LLVM_MINGW "$ENV{HOME}/tools/llvm-mingw")
endif()
if(NOT MINGW_DEPS)
	set(MINGW_DEPS "$ENV{HOME}/tools/mingw-ucrt64")
endif()
set(LLVM_MINGW "${LLVM_MINGW}" CACHE PATH "llvm-mingw installation")
set(MINGW_DEPS "${MINGW_DEPS}" CACHE PATH "Directory containing MSYS2 ucrt64/")

set(TARGET x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   "${LLVM_MINGW}/bin/${TARGET}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/${TARGET}-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW}/bin/${TARGET}-windres")

set(CMAKE_FIND_ROOT_PATH "${MINGW_DEPS}/ucrt64" "${LLVM_MINGW}/${TARGET}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
