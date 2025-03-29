cmake_minimum_required(VERSION 3.14)
project(vcam)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
pkg_check_modules(LIBEXIF REQUIRED libexif)

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

    usbthing/device.c
    usbthing/usbstring.c
    usbthing/vhci.c
)

get_filename_component(VCAM_PWD "${PROJECT_SOURCE_DIR}" ABSOLUTE)

set(VCAM_COMMON_FLAGS -g -Wall -fPIC -Wshadow -Wcast-qual -Wpedantic -Werror=incompatible-pointer-types -Wstrict-aliasing=3)
set(VCAM_COMMON_DEFINITIONS PWD="${VCAM_PWD}")
set(VCAM_INCLUDE_DIRS ${PROJECT_SOURCE_DIR} src usbthing src/fuji src/canon)

add_library(usb-vcam SHARED ${VCAM_CORE} usbthing/libusb.c)
target_link_libraries(usb-vcam ${LIBEXIF_LIBRARIES})
target_include_directories(usb-vcam PRIVATE ${VCAM_INCLUDE_DIRS} ${LIBEXIF_INCLUDE_DIRS} ${LIBUSB_INCLUDE_DIRS})
target_compile_options(usb-vcam PRIVATE ${VCAM_COMMON_FLAGS})
target_compile_definitions(usb-vcam PRIVATE ${VCAM_COMMON_DEFINITIONS})

add_executable(vcam ${VCAM_CORE} src/main.c)
target_link_libraries(vcam ${LIBUSB_LIBRARIES} ${LIBEXIF_LIBRARIES})
target_include_directories(vcam PRIVATE ${VCAM_INCLUDE_DIRS} ${LIBEXIF_INCLUDE_DIRS} ${LIBUSB_INCLUDE_DIRS})
target_compile_options(vcam PRIVATE ${VCAM_COMMON_FLAGS})
target_compile_definitions(vcam PRIVATE ${VCAM_COMMON_DEFINITIONS})
