# Minimum CMake version
cmake_minimum_required(VERSION 3.24)

# Project name
set(PROJECT_NAME "Deferred")
project(${PROJECT_NAME})

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect source/header files recursively
function(target_add_source_dir TARGET_NAME DIR)
    file(GLOB DIR_HEADERS CONFIGURE_DEPENDS
        "${DIR}/*.h"
        "${DIR}/*.hpp"
    )

    file(GLOB DIR_SOURCES CONFIGURE_DEPENDS
        "${DIR}/*.cpp"
        "${DIR}/*.cxx"
        "${DIR}/*.cc"
    )

    target_sources(${TARGET_NAME}
        PRIVATE
            ${DIR_HEADERS}
            ${DIR_SOURCES}
    )

    source_group(
        TREE "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
        ${DIR_HEADERS}
        ${DIR_SOURCES}
        )
endfunction()

add_executable(${PROJECT_NAME}
    ${HEADERS}
    ${SOURCES}
)

target_add_source_dir(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}")
target_add_source_dir(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/common")
target_add_source_dir(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/gltf_parser")
target_add_source_dir(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/resource_managers")
target_add_source_dir(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/passes")

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_BINARY_DIR}/include")

message(STATUS "XXXXXXXXXXXXX = ${CMAKE_BINARY_DIR}/include")

#configure OTCV
set(OTCV_SPIRV_CROSS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/SPIRV-Cross")
set(OTCV_GLFW_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/glfw-3.4.bin.WIN64")
set(OTCV_GLFW_LIB_VARIANT "lib-vc2022")
option(OTCV_ENABLE_IMGUI "" ON)
set(OTCV_IMGUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/imgui")
set(OTCV_ENABLE_SHADER_TOOL ON)
set(OTCV_GLSL_IN_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OTCV_GLSL_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common")
set(OTCV_SPIRV_OUT_PATH "${CMAKE_BINARY_DIR}/spirv")
set(OTCV_GLSL_REFLECT_HEADER_OUT_PATH "${CMAKE_BINARY_DIR}/include/glsl_reflect")
set(OTCV_ENABLE_FRAME_GRAPH ON)
set(OTCV_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../otcv")
add_subdirectory(${OTCV_PATH} ${CMAKE_BINARY_DIR}/otcv)

target_link_libraries(${PROJECT_NAME}
    PRIVATE ${OTCV_BASE_LIB}
    PRIVATE ${OTCV_IMGUI_LIB}
    PRIVATE ${OTCV_FRAME_GRAPH_LIB})
add_dependencies(${PROJECT_NAME} ${OTCV_SHADER_TOOL})

# copy font resource to build directory
#file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fonts DESTINATION ${CMAKE_BINARY_DIR})

# set tinygltf path
set(TINYGLTF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/tinygltf")
message(STATUS "TINYGLTF_PATH = ${TINYGLTF_PATH}")
target_include_directories(${PROJECT_NAME} PUBLIC ${TINYGLTF_PATH})

# set tinyddsloader path
set(TINYDDSLOADER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libraries/tinyddsloader")
message(STATUS "TINYDDSLOADER_PATH = ${TINYDDSLOADER_PATH}")
target_include_directories(${PROJECT_NAME} PUBLIC ${TINYDDSLOADER_PATH})

# copy assets to build folder 
file(COPY
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    DESTINATION "${CMAKE_BINARY_DIR}"
)
