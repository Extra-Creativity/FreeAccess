cmake_minimum_required(VERSION 3.30)
project(FreeAccess LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(FreeAccess INTERFACE)
target_include_directories(FreeAccess INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_options(FreeAccess INTERFACE -freflection)
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(FreeAccess INTERFACE
    -fexpansion-statements
    -fannotation-attributes
    -fparameter-reflection
  )
endif()

if (FREE_ACCESS_BUILD_TESTS)
  add_subdirectory(tests)
endif()