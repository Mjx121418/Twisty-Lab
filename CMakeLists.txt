cmake_minimum_required(VERSION 3.25)
project(twisty_lab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)

add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic)
if(EMSCRIPTEN)
  target_compile_options(project_options INTERFACE -fwasm-exceptions)
  target_link_options(project_options INTERFACE -fwasm-exceptions)
endif()

add_library(json_headers INTERFACE)
target_include_directories(json_headers SYSTEM INTERFACE third_party)

add_library(puzzle_core src/core.cpp src/sha256.cpp)
target_include_directories(puzzle_core PUBLIC include)
target_link_libraries(puzzle_core PUBLIC json_headers project_options)

add_library(puzzle_compiler src/compiler.cpp)
target_link_libraries(puzzle_compiler PUBLIC puzzle_core)

add_library(puzzle_session src/session.cpp src/notation.cpp)
target_link_libraries(puzzle_session PUBLIC puzzle_compiler)

add_library(puzzle_geometry src/geometry.cpp src/catalog_geometry.cpp)
target_link_libraries(puzzle_geometry PUBLIC puzzle_core)

if(EMSCRIPTEN)
  add_executable(twisty_wasm src/bindings.cpp)
  target_link_libraries(twisty_wasm PRIVATE puzzle_session puzzle_geometry embind)
  set_target_properties(twisty_wasm PROPERTIES
    OUTPUT_NAME twisty SUFFIX .mjs
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/web/generated")
  target_link_options(twisty_wasm PRIVATE
    --no-entry
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=1048576
    --emit-tsd "${CMAKE_SOURCE_DIR}/web/generated/twisty.d.mts")
else()
  add_executable(twisty src/cli.cpp)
  target_link_libraries(twisty PRIVATE puzzle_session)
  if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(core_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_executable(geometry_probe tests/geometry_probe.cpp)
    target_link_libraries(geometry_probe PRIVATE puzzle_session puzzle_geometry)
    add_test(NAME core_and_realizations COMMAND core_tests)
    add_executable(helicopter_verify tests/helicopter_verify.cpp)
    target_link_libraries(helicopter_verify PRIVATE puzzle_core)
    target_compile_definitions(helicopter_verify PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(helicopter_verify PRIVATE -O2)
    add_test(NAME helicopter_shape_graph COMMAND helicopter_verify)
    add_test(NAME portable_fixtures COMMAND twisty verify-fixtures --json)
    set_tests_properties(portable_fixtures PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()
