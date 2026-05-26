cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "v1.1.0") 

if(WIN32)
    set(CMAKE_USER_MAKE_RULES_OVERRIDE_C
        "${CMAKE_SOURCE_DIR}/cmake/windows-c-rules-override.cmake")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=old-style-declaration" APPEND)
project(EVION_Firmware)
