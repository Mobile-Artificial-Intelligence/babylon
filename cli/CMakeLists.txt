cmake_minimum_required(VERSION 3.18)
project(babylon_cli)

add_executable(babylon_cli main.cpp)

# OUTPUT_NAME gives the binary the name "babylon"
set_target_properties(babylon_cli PROPERTIES OUTPUT_NAME babylon)

target_link_libraries(babylon_cli babylon)

# On non-Apple Unix, embed the rpath so the binary finds libbabylon next to it
if(NOT APPLE AND NOT WIN32)
    set_target_properties(babylon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()
