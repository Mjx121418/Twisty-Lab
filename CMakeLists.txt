cmake_minimum_required(VERSION 3.25)
project(twisty_lab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(project_options INTERFACE)
target_compile_options(project_options INTERFACE -Wall -Wextra -Wpedantic)
if(EMSCRIPTEN)
  target_compile_options(project_options INTERFACE -fwasm-exceptions)
  target_link_options(project_options INTERFACE -fwasm-exceptions)
endif()

add_library(json_headers INTERFACE)
target_include_directories(json_headers SYSTEM INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

add_library(puzzle_core src/core.cpp src/sha256.cpp)
target_include_directories(puzzle_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_core PUBLIC cxx_std_20)
target_link_libraries(puzzle_core PUBLIC json_headers project_options)

add_library(puzzle_compiler src/compiler.cpp)
target_link_libraries(puzzle_compiler PUBLIC puzzle_core)

add_library(puzzle_session src/session.cpp src/notation.cpp)
target_link_libraries(puzzle_session PUBLIC puzzle_compiler)

add_library(puzzle_geometry src/geometry.cpp src/catalog_geometry.cpp src/spherical_geometry.cpp src/catalog_spherical_geometry.cpp)
target_link_libraries(puzzle_geometry PUBLIC puzzle_core)

add_library(puzzle_kernel INTERFACE)
target_link_libraries(puzzle_kernel INTERFACE puzzle_session puzzle_geometry)
foreach(component IN ITEMS core compiler session geometry kernel)
  add_library(Twisty::${component} ALIAS puzzle_${component})
  set_target_properties(puzzle_${component} PROPERTIES EXPORT_NAME ${component})
endforeach()

if(EMSCRIPTEN)
  add_executable(twisty_wasm src/bindings.cpp)
  target_link_libraries(twisty_wasm PRIVATE puzzle_session puzzle_geometry embind)
  set_target_properties(twisty_wasm PROPERTIES
    OUTPUT_NAME twisty SUFFIX .mjs
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/kernel/generated")
  target_link_options(twisty_wasm PRIVATE
    --no-entry
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=1048576
    --emit-tsd "${CMAKE_SOURCE_DIR}/kernel/generated/twisty.d.mts")
else()
  install(TARGETS puzzle_core puzzle_compiler puzzle_session puzzle_geometry puzzle_kernel
    json_headers project_options EXPORT TwistyKernelTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/twisty DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES third_party/nlohmann/json.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nlohmann)
  install(FILES third_party/nlohmann/LICENSE.MIT
    DESTINATION ${CMAKE_INSTALL_DATADIR}/TwistyKernel/licenses)
  configure_package_config_file(cmake/TwistyKernelConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TwistyKernel)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
  install(EXPORT TwistyKernelTargets NAMESPACE Twisty::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TwistyKernel)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TwistyKernel)
  add_executable(twisty src/cli.cpp)
  target_link_libraries(twisty PRIVATE puzzle_session)
  if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(core_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_executable(geometry_probe tests/geometry_probe.cpp)
    target_link_libraries(geometry_probe PRIVATE puzzle_session puzzle_geometry)
    add_test(NAME core_and_realizations COMMAND core_tests)
    add_executable(spherical_geometry_tests tests/spherical_geometry_tests.cpp)
    target_link_libraries(spherical_geometry_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(spherical_geometry_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(spherical_geometry_tests PRIVATE -O2)
    add_test(NAME spherical_disk_realization COMMAND spherical_geometry_tests)
    add_executable(helicopter_spherical_tests tests/helicopter_spherical_tests.cpp)
    target_link_libraries(helicopter_spherical_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(helicopter_spherical_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(helicopter_spherical_tests PRIVATE -O2)
    add_test(NAME helicopter_spherical_realization COMMAND helicopter_spherical_tests)
    add_executable(helicopter_verify tests/helicopter_verify.cpp)
    target_link_libraries(helicopter_verify PRIVATE puzzle_core)
    target_compile_definitions(helicopter_verify PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(helicopter_verify PRIVATE -O2)
    add_test(NAME helicopter_shape_graph COMMAND helicopter_verify)
    add_executable(bagua_tests tests/bagua_tests.cpp)
    target_link_libraries(bagua_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(bagua_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(bagua_tests PRIVATE -O2)
    add_test(NAME bagua_model_and_realization COMMAND bagua_tests)
    add_test(NAME portable_fixtures COMMAND twisty verify-fixtures --json)
    set_tests_properties(portable_fixtures PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()
