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
set(SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/object_detection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/slicing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/toolbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/hailo_infer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/logging.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# ============================================================
# Include dirs
# ============================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/labels
)

# ============================================================
# Compile options
# ============================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -O3
    -Wno-ignored-qualifiers
    -Wno-extra
    -Wno-reorder
)

if(DEFINED LOG_LEVEL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LOG_LEVEL=${LOG_LEVEL})
endif()

# ============================================================
# Link libs
# ============================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    HailoRT::libhailort
    Threads::Threads
    ${OpenCV_LIBS}
    yaml-cpp
)
