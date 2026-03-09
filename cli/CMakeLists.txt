cmake_minimum_required(VERSION 3.18)
project(babylon_cli)

add_executable(babylon_cli main.cpp)

# OUTPUT_NAME gives the binary the name "babylon"
set_target_properties(babylon_cli PROPERTIES OUTPUT_NAME babylon)

target_include_directories(babylon_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/submodules/json/single_include
)

target_link_libraries(babylon_cli babylon)

# On non-Apple Unix, embed the rpath so the binary finds sibling libs in bin/
if(NOT APPLE AND NOT WIN32)
    set_target_properties(babylon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )
endif()
