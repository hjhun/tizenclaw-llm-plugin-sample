cmake_minimum_required(VERSION 3.10)
project(tizenclaw-llm-plugin-sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(plugin-sample SHARED
    src/plugin-sample.cc
)

# Link against tizenclaw-llm-plugin for headers
find_package(PkgConfig REQUIRED)
pkg_check_modules(PLUGIN_REQUIRED REQUIRED
    tizenclaw-llm-backend
    libcurl
    dlog
)

target_include_directories(plugin-sample
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${PLUGIN_REQUIRED_INCLUDE_DIRS}
)
target_link_libraries(plugin-sample PRIVATE ${PLUGIN_REQUIRED_LIBRARIES})

# Install rules
# For Tizen RPK, files usually go into /opt/usr/globalapps/<pkgname>/res
set(RPK_APP_DIR /opt/usr/globalapps/org.tizen.tizenclaw-llm-plugin-sample)

install(TARGETS plugin-sample
    LIBRARY DESTINATION ${RPK_APP_DIR}/lib
)

install(FILES res/plugin_llm_config.json
    DESTINATION ${RPK_APP_DIR}/res
)
