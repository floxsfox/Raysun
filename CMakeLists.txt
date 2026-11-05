cmake_minimum_required(VERSION 3.22)

project(ray_sun LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Path ke preloader-android (disediakan oleh workflow GitHub Actions)
set(PRELOADER_ANDROID_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../levilaunchroid/app/src/main/cpp/preloader"
    CACHE PATH "Path to LiteLDev/preloader-android")

if(NOT EXISTS "${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
    message(FATAL_ERROR
        "PRELOADER_ANDROID_ROOT salah. Tidak ketemu: "
        "${PRELOADER_ANDROID_ROOT}/include/pl/Config.hpp")
endif()

include(FetchContent)

set(VO_DEPS)
if(NOT TARGET nlohmann_json::nlohmann_json)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
    list(APPEND VO_DEPS nlohmann_json)
endif()
if(NOT TARGET Boost::pfr)
    FetchContent_Declare(boost_pfr
        GIT_REPOSITORY https://github.com/boostorg/pfr.git GIT_TAG boost-1.88.0)
    list(APPEND VO_DEPS boost_pfr)
endif()
if(NOT TARGET magic_enum::magic_enum)
    FetchContent_Declare(magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git GIT_TAG v0.9.7)
    list(APPEND VO_DEPS magic_enum)
endif()
if(NOT TARGET fmt::fmt)
    FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.2.0)
    set(FMT_DOC OFF CACHE BOOL "" FORCE)
    set(FMT_TEST OFF CACHE BOOL "" FORCE)
    set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
    list(APPEND VO_DEPS fmt)
endif()
if(VO_DEPS)
    FetchContent_MakeAvailable(${VO_DEPS})
endif()

if(NOT ANDROID)
    message(FATAL_ERROR "Project ini hanya untuk Android arm64-v8a")
endif()

if(NOT TARGET preloader)
    add_subdirectory("${PRELOADER_ANDROID_ROOT}" "${CMAKE_BINARY_DIR}/_preloader_android")
endif()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}")
find_library(VO_LOG_LIB log)

add_library(ray_sun SHARED RaySun.cpp)
set_target_properties(ray_sun PROPERTIES OUTPUT_NAME "ray_sun")
target_include_directories(ray_sun PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${PRELOADER_ANDROID_ROOT}/include")
target_link_libraries(ray_sun PRIVATE
    nlohmann_json::nlohmann_json Boost::pfr magic_enum::magic_enum fmt::fmt preloader)
if(VO_LOG_LIB)
    target_link_libraries(ray_sun PRIVATE "${VO_LOG_LIB}")
endif()
target_compile_options(ray_sun PRIVATE
    -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden)
