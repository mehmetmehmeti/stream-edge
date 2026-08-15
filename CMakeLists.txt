cmake_minimum_required(VERSION 3.30.4)

project(StreamEdgeProduct LANGUAGES CXX)

option(STREAM_EDGE_BUILD_SERVICE
    "Build the standalone Drogon streaming service executable" ON)
option(STREAM_EDGE_BUILD_FRONTEND
    "Build the Qt frontend submodule from this streaming-edge parent repo" OFF)
option(STREAM_EDGE_FRONTEND_EMBEDDED_PIPELINE
    "Link and run the Holoscan pipeline inside the Qt frontend process" OFF)
option(STREAM_EDGE_FRONTEND_CUDA_OPENGL_INTEROP
    "Enable CUDA/OpenGL interop in the Qt frontend preview item" OFF)
option(STREAM_EDGE_BUILD_TESTS
    "Build streaming-edge tests" ${PROJECT_IS_TOP_LEVEL})

set(_stream_edge_build_embedded_pipeline OFF)
if(STREAM_EDGE_BUILD_SERVICE OR
   (STREAM_EDGE_BUILD_FRONTEND AND STREAM_EDGE_FRONTEND_EMBEDDED_PIPELINE))
    set(_stream_edge_build_embedded_pipeline ON)
endif()

set(STREAMING_SERVICE_BUILD_APP
    ${STREAM_EDGE_BUILD_SERVICE}
    CACHE BOOL "" FORCE)
set(STREAMING_SERVICE_BUILD_RUNTIME
    ${_stream_edge_build_embedded_pipeline}
    CACHE BOOL "" FORCE)
set(STREAMING_SERVICE_BUILD_TESTS
    ${STREAM_EDGE_BUILD_TESTS}
    CACHE BOOL "" FORCE)
set(STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE
    ${_stream_edge_build_embedded_pipeline}
    CACHE BOOL "" FORCE)

add_subdirectory(streaming-service)

if(STREAM_EDGE_BUILD_FRONTEND)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/frontend/CMakeLists.txt")
        message(FATAL_ERROR
            "frontend submodule not found. Run: git submodule update --init --recursive")
    endif()

    set(FRONTEND_ENABLE_STREAMING_EDGE ON CACHE BOOL "" FORCE)
    set(FRONTEND_ENABLE_EMBEDDED_HOLOSCAN_PIPELINE
        ${STREAM_EDGE_FRONTEND_EMBEDDED_PIPELINE}
        CACHE BOOL "" FORCE)
    set(FRONTEND_ENABLE_CUDA_OPENGL_INTEROP
        ${STREAM_EDGE_FRONTEND_CUDA_OPENGL_INTEROP}
        CACHE BOOL "" FORCE)

    add_subdirectory(frontend)
endif()
