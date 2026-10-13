cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR})

project(dunedynasty)
set(DUNE_DYNASTY_VERSION 1.6.4)

set(DUNE_DATA_DIR "." CACHE PATH "Directory containing game data")
option(WITH_ACODEC "FLAC and Ogg Vorbis music" ON)

if(WIN32 OR APPLE)
	set(WITH_ALSA_MIDI OFF)
else()
	option(WITH_ALSA_MIDI "ALSA MIDI music" ON)
endif()

option(WITH_AUD "AUD music (Dune 2000)" ON)
option(WITH_ENET "ENet (multiplayer)" ON)
option(WITH_FLUIDSYNTH "FluidSynth MIDI music" ON)
option(WITH_MAD "MP3 music" ON)
option(PANDORA "Set to ON if targeting an OpenPandora device")

if(NOT DUNE_DATA_DIR)
	set(DUNE_DATA_DIR ".")
endif(NOT DUNE_DATA_DIR)

# ----------------------------------------------------------------

if(MINGW)
	# Work around cmake bug #12480 when cross-compiling.
	# Fixed in cmake commit 6ddb1e5b
	include(Platform/Windows-windres)
elseif(PANDORA)
	add_definitions(-D__PANDORA__)
endif()

add_definitions(-D_DEFAULT_SOURCE)

if(MSDOS)
	# DJGPP hides POSIX declarations (PATH_MAX, struct stat, ...) under -std=c99.
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
else(MSDOS)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
endif(MSDOS)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -pedantic")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wall -Wno-unused-parameter")

if(MSDOS)
	# MS-DOS: Allegro 4.2 platform layer, no optional desktop libraries.
	set(WITH_ACODEC OFF)
	set(WITH_ALSA_MIDI OFF)
	set(WITH_AUD OFF)
	set(WITH_ENET OFF)
	set(WITH_FLUIDSYNTH OFF)
	set(WITH_MAD OFF)

	# Link-time optimisation inlines the game's many one-line helpers
	# across files: about 10% faster per frame on a 486DX2.  DJGPP's
	# binutils have no LTO plugin, so objects carry normal code too.
	option(DOS_LTO "Link-time optimisation for the MS-DOS build" ON)
	if(DOS_LTO)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto=auto -ffat-lto-objects")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto=auto -ffat-lto-objects")
		# COFF has no symbol visibility; LTO warns when it would hide statics.
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=auto -Wno-attributes")
	endif()

	set(DOS_CPU "i486" CACHE STRING "Target CPU for -march (i486, pentium, pentium-mmx, pentiumpro, pentium2, pentium3)")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=${DOS_CPU}")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${DOS_CPU}")

	set(ALLEGRO4_DIR "$ENV{HOME}/tools/src/allegro-4.2" CACHE PATH "Allegro 4.2 source tree built for DJGPP")
	if(NOT EXISTS "${ALLEGRO4_DIR}/lib/djgpp/liballeg.a")
		message(FATAL_ERROR "Allegro 4.2 for DJGPP not found in ${ALLEGRO4_DIR}; run scripts/dos/setup-toolchain.sh")
	endif()
	include_directories(SYSTEM "${ALLEGRO4_DIR}/include")
	# The few Allegro 5 calls in shared code, implemented for DOS
	# (src/dos/a5compat.c): there is no Allegro 5 for DOS.
	include_directories(BEFORE "${CMAKE_SOURCE_DIR}/src/dos/a5compat")
	# Allegro 4.2 predates DJGPP's <stdint.h> and would #define int8_t etc.
	add_definitions(-DALLEGRO_HAVE_STDINT_H)
	# Allegro 4.2 headers use GNU89 "extern inline" semantics.
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
	list(APPEND OPTIONAL_LIBRARIES "${ALLEGRO4_DIR}/lib/djgpp/liballeg.a" m)

	# Directories whose DUNEDYN/ is refreshed after every build (e.g. a
	# share the test machines copy from). Empty: package only on demand.
	set(DOS_PUBLISH_DIRS "" CACHE STRING "Semicolon-separated directories to update with the packaged build")
else(MSDOS)

include(FindPkgConfig)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")

set(ALLEGRO_VERSION 5)
pkg_check_modules(ALLEGRO5 allegro-${ALLEGRO_VERSION})
if(NOT ALLEGRO5_FOUND)
	set(ALLEGRO_VERSION 5.0)
	pkg_check_modules(ALLEGRO5 REQUIRED allegro-${ALLEGRO_VERSION})
endif(NOT ALLEGRO5_FOUND)

pkg_check_modules(ALLEGRO5_AUDIO REQUIRED allegro_audio-${ALLEGRO_VERSION})
pkg_check_modules(ALLEGRO5_IMAGE REQUIRED allegro_image-${ALLEGRO_VERSION})
pkg_check_modules(ALLEGRO5_MEMFILE REQUIRED allegro_memfile-${ALLEGRO_VERSION})
pkg_check_modules(ALLEGRO5_PRIMITIVES REQUIRED allegro_primitives-${ALLEGRO_VERSION})
if(APPLE)
	pkg_check_modules(ALLEGRO5_MAIN REQUIRED allegro_main-${ALLEGRO_VERSION})
endif(APPLE)
include_directories(SYSTEM ${ALLEGRO5_INCLUDE_DIRS})

if(PANDORA)
	find_package(OpenGLES)
	include_directories(SYSTEM ${OPENGLES_INCLUDE_DIR})
	set(OPENGL_LIBRARIES ${OPENGLES_LIBRARIES})
else(PANDORA)
	find_package(OpenGL)
	include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
endif(PANDORA)

if(WITH_ACODEC)
    pkg_check_modules(ALLEGRO5_ACODEC allegro_acodec-${ALLEGRO_VERSION})
    if(ALLEGRO5_ACODEC_FOUND)
	list(APPEND OPTIONAL_LIBRARIES ${ALLEGRO5_ACODEC_LIBRARIES})
    elseif(ALLEGRO5_ACODEC_FOUND)
	message("Allegro 5 audio codecs addon not found")
	set(WITH_ACODEC OFF)
    endif(ALLEGRO5_ACODEC_FOUND)
endif(WITH_ACODEC)

if(WIN32)
	list(APPEND OPTIONAL_LIBRARIES winmm)
elseif(APPLE)
	find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
	find_library(AUDIOUNIT_LIBRARY AudioUnit)
	list(APPEND OPTIONAL_LIBRARIES ${AUDIOTOOLBOX_LIBRARY} ${AUDIOUNIT_LIBRARY})
elseif(WITH_ALSA_MIDI)
    pkg_check_modules(ALSA alsa)
    if(ALSA_FOUND)
	include_directories(SYSTEM ${ALSA_INCLUDE_DIRS})
	list(APPEND OPTIONAL_LIBRARIES ${ALSA_LIBRARIES})
    else(ALSA_FOUND)
	message("ALSA not found")
	set(WITH_ALSA_MIDI OFF)
    endif(ALSA_FOUND)
endif()

if(WITH_ENET)
    find_package(ENet REQUIRED)
    if(ENET_FOUND)
	include_directories(SYSTEM ${ENet_INCLUDE_DIRS})
	list(APPEND OPTIONAL_LIBRARIES ${ENet_LIBRARIES})
    else(ENET_FOUND)
	message("ENet not found")
    endif(ENET_FOUND)
endif(WITH_ENET)

if(WITH_FLUIDSYNTH)
    pkg_check_modules(FLUIDSYNTH REQUIRED fluidsynth)
    if(FLUIDSYNTH_FOUND)
	include_directories(SYSTEM ${FLUIDSYNTH_INCLUDE_DIRS})
	list(APPEND OPTIONAL_LIBRARIES ${FLUIDSYNTH_LIBRARIES})
    else(FLUIDSYNTH_FOUND)
	message("FluidSynth not found")
	set(WITH_FLUIDSYNTH OFF)
    endif(FLUIDSYNTH_FOUND)
endif(WITH_FLUIDSYNTH)

if(WITH_MAD)
    if(MINGW)
	# MINGW MAD missing a package config file.
	set(MAD_LIBRARIES "mad")
	set(MAD_FOUND ON)
    else(MINGW)
	pkg_check_modules(MAD REQUIRED mad)
    endif(MINGW)

    if(MAD_FOUND)
	include_directories(SYSTEM ${MAD_INCLUDE_DIRS})
	list(APPEND OPTIONAL_LIBRARIES ${MAD_LIBRARIES})
    else(MAD_FOUND)
	message("MAD not found")
	set(WITH_MAD OFF)
    endif(MAD_FOUND)
endif(WITH_MAD)

endif(MSDOS)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "dist")
configure_file("${CMAKE_SOURCE_DIR}/include/buildcfg.h.in" "${PROJECT_BINARY_DIR}/include/buildcfg.h")

include(cmake/FileList.cmake)
include_directories("include" "${PROJECT_BINARY_DIR}/include")

if(MSDOS)
	add_executable(dunedynasty ${DUNEDYNASTY_SRC_FILES})
	set_target_properties(dunedynasty PROPERTIES OUTPUT_NAME "dunedyn")
	if(DOS_PUBLISH_DIRS)
		add_custom_command(TARGET dunedynasty POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E env EXE=$<TARGET_FILE_DIR:dunedynasty>/dunedyn.exe
				sh ${CMAKE_SOURCE_DIR}/scripts/dos/package.sh ${DOS_PUBLISH_DIRS}
			COMMENT "Publishing DUNEDYN to ${DOS_PUBLISH_DIRS}"
			VERBATIM)
	endif()
else(MSDOS)
	add_executable(dunedynasty WIN32
		${DUNEDYNASTY_SRC_FILES} ${CMAKE_SOURCE_DIR}/src/icon/icon.rc)
endif(MSDOS)

target_link_libraries(dunedynasty
	${OPTIONAL_LIBRARIES}
	${ALLEGRO5_AUDIO_LDFLAGS}
	${ALLEGRO5_IMAGE_LDFLAGS}
	${ALLEGRO5_MEMFILE_LDFLAGS}
	${ALLEGRO5_PRIMITIVES_LDFLAGS}
	${ALLEGRO5_MAIN_LDFLAGS}
	${ALLEGRO5_LDFLAGS}
	${OPENGL_LIBRARIES}
	)

if(CMAKE_MACOSX_BUNDLE)
	set_target_properties(dunedynasty PROPERTIES
		BUNDLE True
		MACOSX_BUNDLE_BUNDLE_VERSION ${DUNE_DYNASTY_VERSION}
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${DUNE_DYNASTY_VERSION}
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/include/MacOSXBundleInfo.plist.in
	)
endif()

# Copy general static files
file(GLOB STATIC_DOC_FILES "${CMAKE_SOURCE_DIR}/LICENSE.txt" "${CMAKE_SOURCE_DIR}/README.txt"  "${CMAKE_SOURCE_DIR}/CHANGES.txt" "${CMAKE_SOURCE_DIR}/enhancement.txt" "${CMAKE_SOURCE_DIR}/static/general/dunedynasty.cfg-sample")
file(GLOB STATIC_LICENCE_FOLDER "${CMAKE_SOURCE_DIR}/licences")
file(GLOB STATIC_RESOURCE_FOLDERS "${CMAKE_SOURCE_DIR}/static/general/campaign" "${CMAKE_SOURCE_DIR}/static/general/data" "${CMAKE_SOURCE_DIR}/static/general/gfx" "${CMAKE_SOURCE_DIR}/static/general/music")
file(COPY ${STATIC_DOC_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/dist")
file(COPY ${STATIC_LICENCE_FOLDER} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/dist")
if(CMAKE_MACOSX_BUNDLE)
	file(COPY ${STATIC_RESOURCE_FOLDERS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/dist/dunedynasty.app/Contents/Resources")
	file(GLOB STATIC_MACOSX_BUNDLE_FILES "${CMAKE_SOURCE_DIR}/static/macos-bundle/**")
	file(COPY ${STATIC_MACOSX_BUNDLE_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/dist")
else()
	file(COPY ${STATIC_RESOURCE_FOLDERS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/dist")
endif()

# Install files for Linux
if(UNIX AND NOT APPLE)
	install(TARGETS dunedynasty DESTINATION "bin")
	install(PROGRAMS "${CMAKE_SOURCE_DIR}/static/linux/dunedynasty-launcher" DESTINATION "bin")
	install(FILES ${STATIC_DOC_FILES} DESTINATION "doc/dunedynasty-${DUNE_DYNASTY_VERSION}")
	install(DIRECTORY ${STATIC_LICENCE_FOLDER} DESTINATION "doc/dunedynasty-${DUNE_DYNASTY_VERSION}")
	install(FILES "${CMAKE_SOURCE_DIR}/static/linux/io.github.gameflorist.dunedynasty.metainfo.xml" DESTINATION "share/metainfo")
	install(FILES "${CMAKE_SOURCE_DIR}/static/linux/io.github.gameflorist.dunedynasty.desktop" DESTINATION "share/applications")
	install(FILES "${CMAKE_SOURCE_DIR}/static/linux/io.github.gameflorist.dunedynasty.png" DESTINATION "share/icons/hicolor/512x512/apps")

	if(IS_ABSOLUTE ${DUNE_DATA_DIR})
		install(DIRECTORY ${STATIC_RESOURCE_FOLDERS} DESTINATION "${DUNE_DATA_DIR}")
	else(IS_ABSOLUTE ${DUNE_DATA_DIR})
		install(DIRECTORY ${STATIC_RESOURCE_FOLDERS} DESTINATION "share/dunedynasty")
	endif(IS_ABSOLUTE ${DUNE_DATA_DIR})
endif(UNIX AND NOT APPLE)

# ----------------------------------------------------------------

#add_executable(allegro_adl
#	src/audio/allegro_adl.cpp
#	src/audio/adl/fmopl.cpp
#	src/audio/adl/opl_dosbox.cpp
#	src/audio/adl/opl_mame.cpp
#	src/audio/adl/sound_adlib.cpp
#	)
#
#target_link_libraries(allegro_adl
#	${ALLEGRO5_LDFLAGS}
#	${ALLEGRO5_AUDIO_LDFLAGS}
#	)

#if(FLUIDSYNTH_FOUND)
#    add_executable(allegro_midi
#	src/audio/allegro_midi.cpp
#	src/audio/xmi/xmidi.cpp
#	)
#
#    target_link_libraries(allegro_midi
#	${ALLEGRO5_LDFLAGS}
#	${ALLEGRO5_AUDIO_LDFLAGS}
#	${FLUIDSYNTH_LDFLAGS}
#	)
#endif(FLUIDSYNTH_FOUND)

#if(MAD_FOUND)
#    add_executable(allegro_mad src/audio/allegro_mad.c)
#
#    set_target_properties(allegro_mad
#	PROPERTIES COMPILE_FLAGS -DTEST)
#
#    target_link_libraries(allegro_mad
#	${ALLEGRO5_AUDIO_LIBRARIES}
#	${MAD_LIBRARIES}
#	)
#endif(MAD_FOUND)

# vim: noexpandtab
