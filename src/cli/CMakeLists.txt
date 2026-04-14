cmake_minimum_required(VERSION 3.18)
project(babylon_cli)

add_executable(babylon_cli main.cpp)

# OUTPUT_NAME gives the binary the name "babylon"
set_target_properties(babylon_cli PROPERTIES
    OUTPUT_NAME babylon
    RUNTIME_OUTPUT_DIRECTORY ${BABYLON_BIN_DIR}
)

target_include_directories(babylon_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/submodules/json/single_include
)

if(WIN32)
    target_link_libraries(babylon_cli PRIVATE babylon_static delayimp)
    target_link_options(babylon_cli PRIVATE "/DELAYLOAD:onnxruntime.dll")
else()
    target_link_libraries(babylon_cli PRIVATE babylon)
endif()

# On Unix-like systems, embed the rpath so the executable finds shared libs in bin/lib/
if(APPLE)
    set_target_properties(babylon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/lib"
    )
elseif(NOT WIN32)
    set_target_properties(babylon_cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib"
    )
endif()

install(
    TARGETS babylon_cli
    RUNTIME DESTINATION ${BABYLON_INSTALL_RUNTIME_DIR}
)
