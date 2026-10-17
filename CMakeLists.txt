cmake_minimum_required(VERSION 3.25)
project(twisty_lab VERSION 0.2.0 LANGUAGES CXX)

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

add_library(puzzle_symmetry src/symmetry.cpp)
target_include_directories(puzzle_symmetry PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_symmetry PUBLIC cxx_std_20)
target_link_libraries(puzzle_symmetry PUBLIC project_options)

add_library(puzzle_compiler src/compiler.cpp src/gear_compiler.cpp)
target_link_libraries(puzzle_compiler PUBLIC puzzle_core puzzle_symmetry)

# The replacement executor is independently linkable and needs only the STL.
# Authoring, JSON, completion policy, and discovery stay in a separate library.
add_library(puzzle_flat_core src/flat_core.cpp)
target_include_directories(puzzle_flat_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_flat_core PUBLIC cxx_std_20)
target_link_libraries(puzzle_flat_core PUBLIC project_options)
add_library(puzzle_relational src/relational_compiler.cpp src/relational_completion.cpp src/relational_api.cpp src/relational_cube.cpp src/relational_gear.cpp)
target_link_libraries(puzzle_relational PUBLIC puzzle_flat_core puzzle_symmetry json_headers)

add_library(puzzle_execution src/execution.cpp)
target_link_libraries(puzzle_execution PUBLIC puzzle_compiler puzzle_relational)

add_library(puzzle_session src/session.cpp src/notation.cpp)
target_link_libraries(puzzle_session PUBLIC puzzle_execution)

add_library(puzzle_geometry src/geometry.cpp src/catalog_geometry.cpp src/spherical_geometry.cpp src/catalog_spherical_geometry.cpp src/articulated_geometry.cpp)
target_link_libraries(puzzle_geometry PUBLIC puzzle_execution)

add_library(puzzle_kernel INTERFACE)
target_link_libraries(puzzle_kernel INTERFACE puzzle_session puzzle_geometry puzzle_relational)
foreach(component IN ITEMS core compiler execution session geometry symmetry flat_core relational kernel)
  add_library(Twisty::${component} ALIAS puzzle_${component})
  set_target_properties(puzzle_${component} PROPERTIES EXPORT_NAME ${component})
endforeach()

if(EMSCRIPTEN)
  add_executable(twisty_wasm src/bindings.cpp)
  target_link_libraries(twisty_wasm PRIVATE puzzle_session puzzle_geometry puzzle_relational embind)
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
  install(TARGETS puzzle_core puzzle_compiler puzzle_execution puzzle_session puzzle_geometry puzzle_symmetry puzzle_flat_core puzzle_relational puzzle_kernel
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
  add_executable(twisty_relational examples/relational/main.cpp)
  target_link_libraries(twisty_relational PRIVATE puzzle_relational)
  if(BUILD_TESTING)
    add_executable(gear_tests tests/gear_tests.cpp)
    target_link_libraries(gear_tests PRIVATE puzzle_kernel)
    target_compile_definitions(gear_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME gear_and_articulated_realizations COMMAND gear_tests)
    add_executable(gear_relational_tests tests/gear_relational_tests.cpp)
    target_link_libraries(gear_relational_tests PRIVATE puzzle_kernel)
    target_compile_definitions(gear_relational_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME gear_relational_migration COMMAND gear_relational_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME relational_gear_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gear_relational.py)
    add_executable(flat_core_tests tests/flat_core_tests.cpp)
    target_link_libraries(flat_core_tests PRIVATE puzzle_relational)
    add_test(NAME flat_core_relations COMMAND flat_core_tests)
    add_executable(cube_relational_tests tests/cube_relational_tests.cpp)
    target_link_libraries(cube_relational_tests PRIVATE puzzle_relational puzzle_core)
    target_compile_definitions(cube_relational_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME cube_relational_migration COMMAND cube_relational_tests)
    add_executable(relational_session_tests tests/relational_session_tests.cpp)
    target_link_libraries(relational_session_tests PRIVATE puzzle_kernel)
    target_compile_definitions(relational_session_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME relational_sessions_and_interpreters COMMAND relational_session_tests)
    add_executable(helicopter_relational_tests tests/helicopter_relational_tests.cpp)
    target_link_libraries(helicopter_relational_tests PRIVATE puzzle_kernel)
    target_compile_definitions(helicopter_relational_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME helicopter_relational_migration COMMAND helicopter_relational_tests)
    add_executable(bagua_relational_tests tests/bagua_relational_tests.cpp)
    target_link_libraries(bagua_relational_tests PRIVATE puzzle_kernel)
    target_compile_definitions(bagua_relational_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME bagua_relational_migration COMMAND bagua_relational_tests)
    add_test(NAME relational_helicopter_package
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/helicopter_relational.py)
    add_test(NAME relational_cube_packages
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/relational-packages.py --native $<TARGET_FILE:twisty_relational>)
    add_test(NAME relational_source_oracle
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/relational_oracle.py
      $<TARGET_FILE:twisty_relational> ${CMAKE_SOURCE_DIR}/examples/relational/coupled.pzl.json)
    add_test(NAME gear_spherical_candidate_audit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gear_spherical_audit.py)
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
    add_executable(bagua_review_tests tests/bagua_review_tests.cpp)
    target_link_libraries(bagua_review_tests PRIVATE puzzle_session)
    target_compile_definitions(bagua_review_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(bagua_review_tests PRIVATE -O2)
    add_test(NAME bagua_independent_review COMMAND bagua_review_tests)
    add_executable(bagua_spherical_tests tests/bagua_spherical_tests.cpp)
    target_link_libraries(bagua_spherical_tests PRIVATE puzzle_session puzzle_geometry)
    target_compile_definitions(bagua_spherical_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(bagua_spherical_tests PRIVATE -O2)
    add_test(NAME bagua_spherical_realization COMMAND bagua_spherical_tests)
    add_test(NAME portable_fixtures COMMAND twisty verify-fixtures --json)
    set_tests_properties(portable_fixtures PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()
