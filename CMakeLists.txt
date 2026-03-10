cmake_minimum_required(VERSION 3.10)
project(tizenclaw-openai-backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(openai_plugin SHARED
    src/openai_plugin.cc
)

# Link against tizenclaw-llm-plugin for headers
find_package(PkgConfig REQUIRED)
pkg_check_modules(PLUGIN_REQUIRED REQUIRED
    tizen-llm-backend
    libcurl
    dlog
)

target_include_directories(openai_plugin
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${PLUGIN_REQUIRED_INCLUDE_DIRS}
)
target_link_libraries(openai_plugin PRIVATE ${PLUGIN_REQUIRED_LIBRARIES})

# Install rules
# For Tizen RPK, files usually go into /opt/usr/globalapps/<pkgname>/res
set(RPK_APP_DIR /opt/usr/globalapps/org.tizen.tizenclaw-openai-backend)

install(TARGETS openai_plugin
    LIBRARY DESTINATION ${RPK_APP_DIR}/lib
)

install(FILES res/plugin_llm_config.json
    DESTINATION ${RPK_APP_DIR}/res
)
