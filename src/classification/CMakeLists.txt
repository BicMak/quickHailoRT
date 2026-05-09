cmake_minimum_required(VERSION 3.10)
project(classifier LANGUAGES CXX)

# ============================================================
# Settings
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Core dependencies
# ============================================================
find_package(HailoRT REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenCV REQUIRED)

message(STATUS "Found OpenCV includes: ${OpenCV_INCLUDE_DIRS}")

# ============================================================
# Target
# ============================================================
set(SOURCES
    ${PROJECT_NAME}.cpp
    ../common/toolbox.cpp
    ../common/hailo_infer.cpp
    ../common/logging.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4 /O2
        /permissive-
        /Zc:__cplusplus
        /EHsc
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -O3
    )
endif()

# LOG_LEVEL: 0=TRACE 1=DEBUG 2=INFO 3=WARN 4=ERROR 5=NONE (default: DEBUG on debug build, INFO on release)
if(DEFINED LOG_LEVEL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE LOG_LEVEL=${LOG_LEVEL})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    HailoRT::libhailort
    Threads::Threads
    ${OpenCV_LIBS}
)
