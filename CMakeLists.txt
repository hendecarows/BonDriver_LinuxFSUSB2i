cmake_minimum_required(VERSION 3.10)

# Project name
project(BonDriver_LinuxFSUSB2i LANGUAGES C CXX)

# System Information (for debug)
message(STATUS "System Information")
message(STATUS "System = ${CMAKE_SYSTEM_NAME}")
message(STATUS "C Compiler = ${CMAKE_C_COMPILER_ID}")
message(STATUS "C++ Compiler = ${CMAKE_CXX_COMPILER_ID}")

# Build Type Configuration
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release")
endif()

# Other Library
add_subdirectory(plog)

# Package
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Source Files
set(SRC_INC "include")
set(SRC_C "recfsusb2i/src")
set(SRC_CXX "src")

add_library(
	${PROJECT_NAME}
	SHARED
	${SRC_C}/message.c
	${SRC_C}/osdepend.c
	${SRC_C}/usbdevfile.c
	${SRC_C}/usbops.c
	${SRC_C}/it9175.c
	${SRC_C}/it9175_usb.c
	${SRC_C}/tsthread.c
	${SRC_CXX}/BonDriver_LinuxFSUSB2i.cpp
	${SRC_CXX}/config.cpp
	${SRC_CXX}/char_code_conv.cpp
	${SRC_CXX}/util.cpp
)

# Standards
set_target_properties(
	${PROJECT_NAME} PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	CXX_EXTENSIONS OFF
)

# Output Name (Remove "lib" prefix)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

# Define Macros
target_compile_definitions(${PROJECT_NAME} PRIVATE PLOG_LOCAL)

# Include Directory
target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	${SRC_INC}
	${SRC_C}
	${SRC_CXX}
)

# Link Library
target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	Threads::Threads
	m
	plog::plog
)
