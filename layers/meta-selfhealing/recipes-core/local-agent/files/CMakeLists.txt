cmake_minimum_required(VERSION 3.16)
project(local-agent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -O2)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
pkg_check_modules(LIBSYSTEMD REQUIRED libsystemd)

# Find paho-mqtt-c
find_path(PAHO_MQTT_C_INCLUDE_DIR NAMES MQTTClient.h)
find_library(PAHO_MQTT_C_LIB NAMES paho-mqtt3c paho-mqtt3a)

if(NOT PAHO_MQTT_C_LIB)
    message(FATAL_ERROR "paho-mqtt-c library not found")
endif()

include_directories(
    include
    ${JSONCPP_INCLUDE_DIRS}
    ${LIBSYSTEMD_INCLUDE_DIRS}
    ${PAHO_MQTT_C_INCLUDE_DIR}
)

set(SOURCES
    src/main.cpp
    src/system_monitor.cpp
    src/network_healer.cpp
    src/mqtt_client.cpp
)

add_executable(local-agent ${SOURCES})

target_link_libraries(local-agent
    PRIVATE
    ${JSONCPP_LIBRARIES}
    ${LIBSYSTEMD_LIBRARIES}
    ${PAHO_MQTT_C_LIB}
    pthread
)

install(TARGETS local-agent DESTINATION bin)
