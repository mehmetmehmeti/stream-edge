cmake_minimum_required(VERSION 3.30.4)

project(streaming_service LANGUAGES CXX)

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local")
list(APPEND CMAKE_PREFIX_PATH "/opt/nvidia/holoscan")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_policy(SET CMP0153 OLD)

option(STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE
    "Build the reusable Holoscan/CUDA streaming pipeline target" ON)
option(STREAMING_SERVICE_BUILD_APP
    "Build the Drogon streaming_service executable" ON)
option(STREAMING_SERVICE_BUILD_RUNTIME
    "Build reusable streaming service runtime components" ${STREAMING_SERVICE_BUILD_APP})
option(STREAMING_SERVICE_BUILD_TESTS
    "Build streaming-service unit tests" ${PROJECT_IS_TOP_LEVEL})

if(STREAMING_SERVICE_BUILD_APP AND NOT STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE)
    message(FATAL_ERROR
        "STREAMING_SERVICE_BUILD_APP requires STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE")
endif()

if(STREAMING_SERVICE_BUILD_APP AND NOT STREAMING_SERVICE_BUILD_RUNTIME)
    message(FATAL_ERROR
        "STREAMING_SERVICE_BUILD_APP requires STREAMING_SERVICE_BUILD_RUNTIME")
endif()

if(STREAMING_SERVICE_BUILD_RUNTIME AND NOT STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE)
    message(FATAL_ERROR
        "STREAMING_SERVICE_BUILD_RUNTIME requires STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE")
endif()

add_library(streaming_frame_bridge STATIC
    src/streaming/FrameBridge.cc
)

target_include_directories(streaming_frame_bridge PUBLIC include)

add_library(streaming_source_config STATIC
    src/streaming/StreamingSourceConfigLoader.cc
    src/streaming/StreamingSourceHealth.cc
)

target_include_directories(streaming_source_config PUBLIC include)

add_library(streaming_recording_core STATIC
    src/streaming/RecordingEngine.cc
)

target_include_directories(streaming_recording_core PUBLIC include)
target_link_libraries(streaming_recording_core PUBLIC streaming_frame_bridge)

if(STREAMING_SERVICE_BUILD_EMBEDDED_PIPELINE)
    if(NOT DEFINED CUDAToolkit_ROOT)
        set(CUDAToolkit_ROOT "/usr/local/cuda-12" CACHE PATH
            "Preferred CUDA toolkit root for Holoscan builds")
    endif()

    if(NOT DEFINED CCCL_ROOT)
        set(CCCL_ROOT "/usr/local/cuda-12/lib64/cmake/cccl" CACHE PATH
            "Preferred CCCL root for Holoscan builds")
    endif()

    find_package(CUDAToolkit REQUIRED)
    find_package(holoscan REQUIRED CONFIG
        PATHS "/opt/nvidia/holoscan"
    )
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET
            gstreamer-1.0
            gstreamer-app-1.0
            gstreamer-video-1.0
        )
        pkg_check_modules(GSTREAMER_CUDA QUIET IMPORTED_TARGET
            gstreamer-cuda-1.0
        )
    endif()

    add_library(streaming_embedded_pipeline STATIC
        src/streaming/HoloscanVideoPipeline.cc
        src/streaming/HoloscanVideoApplication.cc
        src/streaming/QtOutputBridgeOp.cc
        src/streaming/RecordingOp.cc
        src/streaming/FrameStagingPool.cc
    )

    if(GSTREAMER_FOUND)
        target_sources(streaming_embedded_pipeline PRIVATE
            src/streaming/GStreamerMp4Writer.cc
        )
        target_link_libraries(streaming_embedded_pipeline PUBLIC PkgConfig::GSTREAMER)
        target_compile_definitions(streaming_embedded_pipeline PRIVATE STREAMING_HAS_GSTREAMER=1)

        if(GSTREAMER_CUDA_FOUND)
            target_link_libraries(streaming_embedded_pipeline PUBLIC
                PkgConfig::GSTREAMER_CUDA
                CUDA::cuda_driver
            )
            target_compile_definitions(streaming_embedded_pipeline
                PRIVATE STREAMING_HAS_GSTREAMER_CUDA=1)
        else()
            message(STATUS
                "GStreamer CUDA development package was not found; CUDA memory recording is disabled")
        endif()
    else()
        message(STATUS
            "GStreamer development packages were not found; MP4 writer is disabled")
    endif()

    target_include_directories(streaming_embedded_pipeline PUBLIC include)

    target_link_libraries(streaming_embedded_pipeline PUBLIC
        streaming_frame_bridge
        streaming_source_config
        streaming_recording_core
        CUDA::cudart
        holoscan::core
        holoscan::ops::format_converter
        holoscan::ops::holoviz
        holoscan::ops::v4l2
        holoscan::ops::video_stream_replayer
    )
endif()

if(STREAMING_SERVICE_BUILD_RUNTIME)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSTREAMER_APP REQUIRED IMPORTED_TARGET
        gstreamer-1.0
        gstreamer-app-1.0
        gstreamer-video-1.0
    )
    find_package(Drogon CONFIG REQUIRED)
    find_package(TinyOrm CONFIG REQUIRED)

    add_library(streaming_service_runtime STATIC
        src/services/CloudPublisher.cc
        src/services/FrameBridgeShmPublisher.cc
        src/services/DatabaseService.cc
        src/services/CommandSyncService.cc
        src/services/StreamingService.cc
    )

    target_include_directories(streaming_service_runtime PUBLIC include)

    target_link_libraries(streaming_service_runtime PUBLIC
        streaming_embedded_pipeline
        Drogon::Drogon
        TinyOrm::TinyOrm
        PkgConfig::GSTREAMER_APP
    )

    set_target_properties(streaming_service_runtime PROPERTIES
        BUILD_RPATH "$ENV{HOME}/.local/lib;/opt/nvidia/holoscan/lib"
        INSTALL_RPATH "$ENV{HOME}/.local/lib;/opt/nvidia/holoscan/lib"
    )
endif()

if(STREAMING_SERVICE_BUILD_APP)
    add_executable(streaming_service
        src/main.cc
        src/controllers/HealthController.cc
        src/controllers/StreamingController.cc
    )

    target_include_directories(streaming_service PRIVATE include)

    target_link_libraries(streaming_service PRIVATE
        streaming_service_runtime
    )

    set_target_properties(streaming_service PROPERTIES
        BUILD_RPATH "$ENV{HOME}/.local/lib;/opt/nvidia/holoscan/lib"
        INSTALL_RPATH "$ENV{HOME}/.local/lib;/opt/nvidia/holoscan/lib"
    )
endif()

if(STREAMING_SERVICE_BUILD_TESTS)
    enable_testing()

    add_executable(frame_bridge_tests
        tests/FrameBridgeTest.cc
    )

    target_link_libraries(frame_bridge_tests PRIVATE streaming_frame_bridge)

    add_test(NAME frame_bridge_tests COMMAND frame_bridge_tests)

    add_executable(recording_config_tests
        tests/RecordingConfigTest.cc
    )

    target_link_libraries(recording_config_tests PRIVATE streaming_frame_bridge)

    add_test(NAME recording_config_tests COMMAND recording_config_tests)

    add_executable(recording_engine_tests
        tests/RecordingEngineTest.cc
    )

    target_link_libraries(recording_engine_tests PRIVATE streaming_recording_core)

    add_test(NAME recording_engine_tests COMMAND recording_engine_tests)

    if(TARGET streaming_embedded_pipeline)
        add_executable(frame_staging_pool_tests
            tests/FrameStagingPoolTest.cc
        )

        target_link_libraries(frame_staging_pool_tests PRIVATE streaming_embedded_pipeline)

        add_test(NAME frame_staging_pool_tests COMMAND frame_staging_pool_tests)

        if(GSTREAMER_FOUND)
            add_executable(gstreamer_mp4_writer_smoke_test
                tests/GStreamerMp4WriterSmokeTest.cc
            )

            target_link_libraries(gstreamer_mp4_writer_smoke_test
                PRIVATE streaming_embedded_pipeline)

            add_test(NAME gstreamer_mp4_writer_smoke_test
                COMMAND gstreamer_mp4_writer_smoke_test)
        endif()
    endif()
endif()
