cmake_minimum_required(VERSION 3.16)
project(SAHI_object_detection LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Core dependencies
# ============================================================
find_package(Threads REQUIRED)
find_package(HailoRT REQUIRED)
find_package(OpenCV REQUIRED)

message(STATUS "Found OpenCV includes: ${OpenCV_INCLUDE_DIRS}")

# ============================================================
# Sources
# ============================================================
set(COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/slicing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/toolbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/hailo_infer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/logging.cpp
)

add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/object_detection.cpp
    ${COMMON_SOURCES}
)

add_executable(${PROJECT_NAME}_video
    ${CMAKE_CURRENT_SOURCE_DIR}/object_detection_video.cpp
    ${COMMON_SOURCES}
)

# ============================================================
# Include dirs / compile options / link libs (apply to both)
# ============================================================
foreach(_tgt ${PROJECT_NAME} ${PROJECT_NAME}_video)
    target_include_directories(${_tgt} PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/labels
    )

    target_compile_options(${_tgt} PRIVATE
        -Wall -Wextra -O3
        -Wno-ignored-qualifiers
        -Wno-extra
        -Wno-reorder
    )

    if(DEFINED LOG_LEVEL)
        target_compile_definitions(${_tgt} PRIVATE LOG_LEVEL=${LOG_LEVEL})
    endif()

    target_link_libraries(${_tgt} PRIVATE
        HailoRT::libhailort
        Threads::Threads
        ${OpenCV_LIBS}
        yaml-cpp
    )
endforeach()
