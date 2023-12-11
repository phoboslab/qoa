cmake_minimum_required(VERSION 3.15)

project(QOA)

option(INCLUDE_FLAC "Add flac functionality to qoaconv" OFF)
option(INCLUDE_MP3 "Add mp3 functionality to qoaconv" OFF)

if (NOT EXISTS ./sokol)
	execute_process(COMMAND git clone https://github.com/floooh/sokol)
endif()

set(QOAConvFlags "")
set(QOAPlayFlags "")

# LICENSING: DrLibs is declared public domain. If it is beneficial, it could be brought in.

set(DRLIBS_URL "https://github.com/mackron/dr_libs")
if (INCLUDE_FLAC)
	if (NOT EXISTS ./dr_libs)
		execute_process(COMMAND git clone ${DRLIBS_URL})
	endif()
	list(APPEND QOAConvFlags -DQOACONV_HAS_DRFLAC)
endif()
if (INCLUDE_MP3)
	if (NOT EXISTS ./dr_libs)
		execute_process(COMMAND git clone ${DRLIBS_URL})
	endif()
	list(APPEND QOAConvFlags -DQOACONV_HAS_DRMP3)
endif()

add_executable(QOAPlay 
					qoaplay.c)
add_executable(QOAConv 
					qoaconv.c)

list(APPEND QOAConvFlags -O3 -Wall -Wextra -std=gnu99)
list(APPEND QOAPlayFlags -O3 -Wall -Wextra -std=gnu99)
target_compile_options(QOAPlay PUBLIC ${QOAPlayFlags})
target_compile_options(QOAConv PUBLIC ${QOAConvFlags})
set(QOAPlayInclude "")
set(QOAConvInclude "")
list(APPEND QOAPlayInclude ./sokol)
if (INCLUDE_MP3 OR INCLUDE_FLAC)
	list(APPEND QOAConvInclude dr_libs/)
endif()
target_include_directories(QOAConv PUBLIC ${QOAConvInclude})
target_include_directories(QOAPlay PUBLIC ${QOAPlayInclude})

if (WIN32)
	target_link_libraries(QOAConv -lm)
	target_link_libraries(QOAPlay -lole32)
elseif (UNIX)
	target_link_libraries(QOAConv -lm)
	target_link_libraries(QOAPlay -lpthread -lasound)
endif()