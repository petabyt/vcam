cmake_minimum_required(VERSION 3.14)
project(vcam)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

include_directories(${LIBUSB_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} src usb src/fuji src/canon)

set(VCAM_CORE 
    src/log.c
    src/vcamera.c
    src/pack.c
    src/packet.c
    src/ops.c
    src/canon/canon.c
    src/fuji/fuji.c
    src/fuji/server.c
    src/ptpip.c
    src/canon/props.c
    src/data.c
    src/props.c
    src/fuji/ssdp.c
    src/socket.c
    src/fuji/usb.c
    src/fuji/fs.c
    src/usbthing.c

    usb/device.c
    usb/usbstring.c
    usb/vhci.c
)

set(VCAM_SO_FILES ${VCAM_CORE} usb/device.c usb/usbstring.c usb/vhci.c)
set(VCAM_VCAM_FILES ${VCAM_CORE} src/main.c)

get_filename_component(VCAM_PWD "${CMAKE_SOURCE_DIR}" ABSOLUTE)

set(VCAM_COMMON_FLAGS -g -Wall -fPIC -Wshadow -Wcast-qual -Wpedantic -Werror=incompatible-pointer-types -Wstrict-aliasing=3)
set(VCAM_COMMON_DEFINITIONS PWD="${VCAM_PWD}")

add_library(usb-vcam SHARED ${VCAM_SO_FILES})
target_link_libraries(usb-vcam ${LIBUSB_LIBRARIES} -lexif)
target_compile_options(usb-vcam PRIVATE ${VCAM_COMMON_FLAGS})
target_compile_definitions(usb-vcam PRIVATE ${VCAM_COMMON_DEFINITIONS})

add_executable(vcam ${VCAM_VCAM_FILES})
target_link_libraries(vcam ${LIBUSB_LIBRARIES} -lexif)
target_compile_options(vcam PRIVATE ${VCAM_COMMON_FLAGS})
target_compile_definitions(vcam PRIVATE ${VCAM_COMMON_DEFINITIONS})
