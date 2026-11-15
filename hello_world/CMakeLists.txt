# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The version (esp_app_get_description()->version, GET /api/v1/version) is the
# one in VERSION at the top of the repository, shared with the web app.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../VERSION PROJECT_VER LIMIT_COUNT 1)
string(STRIP "${PROJECT_VER}" PROJECT_VER)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/../VERSION)

project(hello_world)

