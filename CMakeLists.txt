cmake_minimum_required (VERSION 2.8)
project (Outrigger)

set(CMAKE_THREAD_PREFER_PTHREAD)
find_package(Threads REQUIRED)
list(APPEND CMAKE_REQUIRED_LIBRARIES c)

include_directories(
	api
	iniparser/src
	io
	io/serial
	os
	rigs/kenwood_hf
	rigs/yaesu_cat
)

set(SOURCES
	api/api.c
	io/io.c
	io/serial/serial.c
	rigs/kenwood_hf/kenwood_hf.c
	rigs/kenwood_hf/ts-140s.c
	rigs/kenwood_hf/ts-440s.c
	rigs/kenwood_hf/ts-711a.c
	rigs/kenwood_hf/ts-940s.c
	rigs/yaesu_cat/yaesu_bincat.c
	rigs/yaesu_cat/ft-736r.c
	iniparser/src/dictionary.c
	iniparser/src/iniparser.c
)

find_path(TERMIOS_PATH termios.h)
find_path(UNISTD_PATH unistd.h)
find_path(SIGNAL_PATH signal.h)

if(CMAKE_USE_WIN32_THREADS_INIT)
	list(APPEND SOURCES os/threads_win32.c)
	list(APPEND SOURCES os/semaphores_win32.c)
	list(APPEND SOURCES os/mutexes_win32.c)
	add_definitions(-DWIN32_THREADS -DWIN32_MUTEXES -DWIN32_SEMAPHORES)
endif()
if(CMAKE_USE_PTHREADS_INIT)
	list(APPEND SOURCES os/threads_posix.c)
	list(APPEND SOURCES os/mutexes_posix.c)
	add_definitions(-DPOSIX_THREADS -DPOSIX_MUTEXES)
endif()

if(TERMIOS_PATH)
	list(APPEND SOURCES io/serial/io_termios.c)
	add_definitions(-DWITH_TERMIOS)
endif()

if(UNISTD_PATH)
	add_definitions(-DWITH_UNISTD)
	check_symbol_exists(_POSIX_TIMERS ${UNISTD_PATH}/unistd.h HAS_POSIX_TIMERS)
	check_symbol_exists(_POSIX_SEMAPHORES ${UNISTD_PATH}/unistd.h HAS_POSIX_SEMAPHORES)
	if(HAS_POSIX_SEMAPHORES AND NOT CMAKE_USE_WIN32_THREADS_INIT)
		list(APPEND SOURCES os/semaphores_posix.c)
		add_definitions(-DPOSIX_SEMAPHORES)
	endif()
	check_symbol_exists(fork ${UNISTD_PATH}/unistd.h HAS_FORK)
	if(HAS_FORK)
		add_definitions(-DWITH_FORK)
	endif()
endif()
if(HAS_POSIX_TIMERS)
	list(APPEND SOURCES os/datetime_posix.c)
	add_definitions(-DPOSIX_DATETIME)
elseif(WIN32)
	list(APPEND SOURCES os/datetime_win32.c)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	# Needed for GetTickCount64
	add_definitions(-DWIN32_DATETIME -D_WIN32_WINNT=0x0600)
	# Needed for snprintf(!)
	add_definitions(-Dsnprintf=_snprintf)
endif()
if(WIN32)
	list(APPEND SOURCES os/sockets_win32)
	add_definitions(-DWIN32_SOCKETS)
else()
	list(APPEND SOURCES os/sockets_bsd)
	add_definitions(-DBSD_SOCKETS)
endif()
if(SIGNAL_PATH)
	check_symbol_exists(signal ${SIGNAL_PATH}/signal.h HAS_SIGNAL)
	if(HAS_SIGNAL)
		add_definitions(-DWITH_SIGNAL)
	endif()
endif()

add_library(outrigger ${SOURCES})

add_executable(testcmds test.c)
target_link_libraries(testcmds outrigger)

add_executable(or-rigctld or-rigctld.c)
target_link_libraries(or-rigctld outrigger)
if(WIN32)
	target_link_libraries(outrigger kernel32)
	target_link_libraries(or-rigctld ws2_32)
endif()

target_link_libraries(outrigger ${CMAKE_THREAD_LIBS_INIT})
