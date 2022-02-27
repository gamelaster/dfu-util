cmake_minimum_required(VERSION 3.15)
project(dfu-util)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11" CACHE STRING "Minimum OS X deployment version")
endif ()

add_executable(dfu-util src/main.c
    src/portable.h
    src/dfu_load.c
    src/dfu_load.h
    src/dfu_util.c
    src/dfu_util.h
    src/dfuse.c
    src/dfuse.h
    src/dfuse_mem.c
    src/dfuse_mem.h
    src/dfu.c
    src/dfu.h
    src/usb_dfu.h
    src/dfu_file.c
    src/dfu_file.h
    src/quirks.c
    src/quirks.h)

if (WIN32)
    target_compile_definitions(dfu-util PRIVATE HAVE_WINDOWS_H _CRT_SECURE_NO_WARNINGS HAVE_STRING_H)
    target_include_directories(dfu-util PRIVATE ../libusb-prebuilt/include/libusb-1.0 msvc/getopt)
    target_sources(dfu-util PRIVATE msvc/getopt/getopt.c msvc/getopt/getopt1.c)
    target_link_directories(dfu-util PRIVATE ../libusb-prebuilt/dll)
    target_link_libraries(dfu-util PRIVATE libusb-1.0)
elseif (APPLE)
    target_include_directories(dfu-util PRIVATE ${CMAKE_SOURCE_DIR}/../outdist/include/libusb-1.0)
    target_link_directories(dfu-util PRIVATE ${CMAKE_SOURCE_DIR}/../outdist/lib) # TODO: Check for pkg-config?
    target_link_libraries(dfu-util PRIVATE libusb-1.0)
    target_compile_definitions(dfu-util PRIVATE HAVE_UNISTD_H HAVE_NANOSLEEP HAVE_ERR HAVE_SYSEXITS_H)
endif ()

add_library(libdfu-util SHARED src/lib.c
    src/portable.h
    src/dfu_load.c
    src/dfu_load.h
    src/dfu_util.c
    src/dfu_util.h
    src/dfuse.c
    src/dfuse.h
    src/dfuse_mem.c
    src/dfuse_mem.h
    src/dfu.c
    src/dfu.h
    src/usb_dfu.h
    src/dfu_file.c
    src/dfu_file.h
    src/quirks.c
    src/quirks.h)

set_target_properties(libdfu-util PROPERTIES
    PUBLIC_HEADER src/lib.h
    VERSION 1.0
    SOVERSION 1
    OUTPUT_NAME "dfu-util"
)

if (WIN32)
    target_compile_definitions(libdfu-util PRIVATE HAVE_WINDOWS_H _CRT_SECURE_NO_WARNINGS)
    target_include_directories(libdfu-util PRIVATE ${CMAKE_SOURCE_DIR}/libusb-1.0.25/libusb ./include/dart-sdk/)
    target_link_directories(libdfu-util PRIVATE ${CMAKE_SOURCE_DIR}/libusb-1.0.25/x64/Release/dll/)
    target_link_libraries(libdfu-util PRIVATE libusb-1.0)
elseif (APPLE)
    target_include_directories(libdfu-util PRIVATE ${CMAKE_SOURCE_DIR}/libusb-1.0.25/libusb  ./include/dart-sdk/)
    target_link_directories(libdfu-util PRIVATE ${CMAKE_SOURCE_DIR}/libusb-1.0.25/libusb/.libs) # TODO: Check for pkg-config?
    target_link_libraries(libdfu-util PRIVATE usb-1.0)
    target_compile_definitions(libdfu-util PRIVATE HAVE_UNISTD_H HAVE_NANOSLEEP HAVE_SYSEXITS_H)
endif ()