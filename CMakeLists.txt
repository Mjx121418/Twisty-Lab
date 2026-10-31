cmake_minimum_required(VERSION 3.25)
project(twisty_lab VERSION 0.7.0 LANGUAGES CXX)

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

add_library(puzzle_catalog src/catalog.cpp src/sha256.cpp)
target_include_directories(puzzle_catalog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_catalog PUBLIC cxx_std_20)
target_link_libraries(puzzle_catalog PUBLIC json_headers project_options puzzle_core)

add_library(puzzle_symmetry src/authoring/symmetry.cpp)
target_include_directories(puzzle_symmetry PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_symmetry PUBLIC cxx_std_20)
target_link_libraries(puzzle_symmetry PUBLIC project_options)

# The executor is independently linkable and needs only the STL.
# Authoring, JSON, completion policy, and discovery stay in a separate library.
add_library(puzzle_core src/core.cpp src/transports.cpp)
target_include_directories(puzzle_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(puzzle_core PUBLIC cxx_std_20)
target_link_libraries(puzzle_core PUBLIC project_options)
add_library(puzzle_runtime src/runtime.cpp src/completion.cpp)
target_link_libraries(puzzle_runtime PUBLIC puzzle_core)
add_library(puzzle_language
  src/authoring/compiler.cpp
  src/authoring/compact.cpp
  src/authoring/compact_cube.cpp
  src/authoring/cube.cpp
  src/authoring/gear.cpp
  src/language_api.cpp)
target_link_libraries(puzzle_language PUBLIC puzzle_runtime puzzle_symmetry json_headers)

add_library(puzzle_execution src/execution.cpp src/controller/catalog.cpp)
target_link_libraries(puzzle_execution PUBLIC puzzle_catalog puzzle_runtime PRIVATE puzzle_language)

add_library(puzzle_session src/session.cpp src/notation.cpp)
target_link_libraries(puzzle_session PUBLIC puzzle_execution)

add_library(puzzle_controller src/controller/controller.cpp)
target_link_libraries(puzzle_controller PUBLIC puzzle_session)

add_library(puzzle_geometry
  src/interpreter/geometry.cpp
  src/interpreter/s3_geometry.cpp
  src/interpreter/geometry_compiler.cpp
  src/interpreter/motion_families.cpp
  src/interpreter/catalog_geometry.cpp
  src/interpreter/spherical_geometry.cpp
  src/interpreter/catalog_spherical_geometry.cpp
  src/interpreter/articulated_geometry.cpp
  src/interpreter/flat_surface_geometry.cpp)
target_link_libraries(puzzle_geometry PUBLIC puzzle_execution)

add_library(puzzle_kernel INTERFACE)
target_link_libraries(puzzle_kernel INTERFACE puzzle_controller puzzle_session puzzle_geometry puzzle_language)
foreach(component IN ITEMS catalog execution session controller geometry symmetry core runtime language kernel)
  add_library(Twisty::${component} ALIAS puzzle_${component})
  set_target_properties(puzzle_${component} PROPERTIES EXPORT_NAME ${component})
endforeach()

if(EMSCRIPTEN)
  add_executable(twisty_wasm src/bindings.cpp)
  target_link_libraries(twisty_wasm PRIVATE puzzle_controller puzzle_geometry puzzle_language embind)
  set_target_properties(twisty_wasm PROPERTIES
    OUTPUT_NAME twisty SUFFIX .mjs
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/kernel/generated")
  target_link_options(twisty_wasm PRIVATE
    --no-entry
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432
    -sMAXIMUM_MEMORY=536870912 -sSTACK_SIZE=1048576
    --emit-tsd "${CMAKE_SOURCE_DIR}/kernel/generated/twisty.d.mts")
else()
  install(TARGETS puzzle_catalog puzzle_execution puzzle_session puzzle_controller puzzle_geometry puzzle_symmetry puzzle_core puzzle_runtime puzzle_language puzzle_kernel
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
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
  install(EXPORT TwistyKernelTargets NAMESPACE Twisty::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TwistyKernel)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TwistyKernelConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TwistyKernel)
  add_executable(twisty src/cli.cpp)
  target_link_libraries(twisty PRIVATE puzzle_session)
  add_executable(twisty_language examples/language/main.cpp)
  target_link_libraries(twisty_language PRIVATE puzzle_language)
  add_executable(melty_solids scripts/melty_solids.cpp)
  target_link_libraries(melty_solids PRIVATE project_options)
  target_compile_options(melty_solids PRIVATE -O2)
  if(BUILD_TESTING)
    add_executable(runtime_tests examples/runtime/main.cpp)
    target_link_libraries(runtime_tests PRIVATE puzzle_runtime)
    add_test(NAME runtime_descriptors COMMAND runtime_tests)
    add_executable(controller_tests tests/controller_tests.cpp)
    target_link_libraries(controller_tests PRIVATE puzzle_kernel)
    target_compile_definitions(controller_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME family_controller COMMAND controller_tests)
    add_executable(transport_bench tests/transport_bench.cpp)
    target_link_libraries(transport_bench PRIVATE puzzle_session)
    add_executable(controller_bench tests/controller_bench.cpp)
    target_link_libraries(controller_bench PRIVATE puzzle_kernel)
    add_executable(compact_parity tests/compact_parity.cpp)
    target_link_libraries(compact_parity PRIVATE puzzle_session)
    target_compile_definitions(compact_parity PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_executable(reference_parity tests/reference_parity.cpp)
    target_link_libraries(reference_parity PRIVATE puzzle_session)
    add_executable(actions_tests tests/actions_tests.cpp)
    target_link_libraries(actions_tests PRIVATE puzzle_core)
    add_test(NAME finite_actions COMMAND actions_tests)
    add_executable(geometry_api_tests tests/geometry_api_tests.cpp)
    target_link_libraries(geometry_api_tests PRIVATE puzzle_kernel)
    target_compile_definitions(geometry_api_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME geometry_api COMMAND geometry_api_tests)
    add_executable(cell24_geometry_tests tests/cell24_geometry_tests.cpp)
    target_link_libraries(cell24_geometry_tests PRIVATE puzzle_kernel)
    target_compile_definitions(cell24_geometry_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME cell24_geometry COMMAND cell24_geometry_tests)
    add_test(NAME cell24_192_geometry COMMAND cell24_geometry_tests cell24-192)
    add_executable(s3_geometry_tests tests/s3_geometry_tests.cpp)
    target_link_libraries(s3_geometry_tests PRIVATE puzzle_kernel)
    target_compile_definitions(s3_geometry_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME s3_geometry COMMAND s3_geometry_tests)
    add_executable(runtime_api_tests tests/runtime_api_tests.cpp)
    target_link_libraries(runtime_api_tests PRIVATE puzzle_kernel)
    target_compile_definitions(runtime_api_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME runtime_all_presets COMMAND runtime_api_tests)
    add_executable(flat_surface_tests tests/flat_surface_tests.cpp)
    target_link_libraries(flat_surface_tests PRIVATE puzzle_kernel)
    target_compile_definitions(flat_surface_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME flat_surfaces COMMAND flat_surface_tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME cell24_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cell24_model_test.py)
    add_test(NAME cell24_192_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/cell24_192_model_test.py)
    add_test(NAME tesseract_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/tesseract_model_test.py)
    add_test(NAME reference_transport_parity COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/reference_parity.py
      $<TARGET_FILE:reference_parity> ${CMAKE_SOURCE_DIR}/tests/fixtures/reference-baseline.jsonl.gz)
    add_test(NAME reference_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/reference_authoring.py)
    add_test(NAME compact_transport_parity COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/compact_parity.py
      $<TARGET_FILE:compact_parity> ${CMAKE_SOURCE_DIR}/tests/fixtures/transport-baseline.json.gz)
    add_test(NAME flat_surface_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/flat_surface_packages.py)
    add_test(NAME hex_surface_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/hex_surface_packages.py)
    add_test(NAME pillowcase_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/pillowcase_packages.py)
    add_test(NAME orbifold333_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/orbifold333_packages.py)
    add_test(NAME runtime_has_no_legacy_backend COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_SOURCE_DIR}/tests/runtime_boundary.py
      $<TARGET_FILE:puzzle_runtime>
      $<TARGET_FILE:puzzle_catalog> $<TARGET_FILE:puzzle_execution>
      $<TARGET_FILE:puzzle_session> $<TARGET_FILE:puzzle_geometry>)
    add_test(NAME gear_packages COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/gear_packages.py)
    add_executable(core_tests tests/core_tests.cpp)
    target_link_libraries(core_tests PRIVATE puzzle_language)
    add_test(NAME core_relations COMMAND core_tests)
    add_executable(melty_tests tests/melty_tests.cpp)
    target_link_libraries(melty_tests PRIVATE puzzle_kernel)
    target_compile_definitions(melty_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME melty_orientations COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/melty_orientations.py)
    add_test(NAME melty_spherical COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/melty_spherical.py)
    add_executable(icosahedron_tests tests/icosahedron_tests.cpp)
    target_link_libraries(icosahedron_tests PRIVATE puzzle_kernel)
    target_compile_definitions(icosahedron_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME icosahedron_kernel COMMAND icosahedron_tests)
    add_test(NAME icosahedron_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/icosahedron_model.py)
    add_test(NAME icosahedron_152_authoring COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/icosahedron_152_model.py)
    foreach(family a b c d)
      add_test(NAME melty_${family} COMMAND melty_tests ${family})
    endforeach()
    add_executable(session_tests tests/session_tests.cpp)
    target_link_libraries(session_tests PRIVATE puzzle_kernel)
    target_compile_definitions(session_tests PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME sessions_and_interpreters COMMAND session_tests)
    add_test(NAME helicopter_package
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/helicopter_packages.py)
    add_test(NAME cube_packages
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/cube-packages.py --native $<TARGET_FILE:twisty_language>)
    add_test(NAME language_source_oracle
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/language_oracle.py
      $<TARGET_FILE:twisty_language> ${CMAKE_SOURCE_DIR}/examples/language/coupled.pzl.json)
    add_executable(geometry_probe tests/geometry_probe.cpp)
    target_link_libraries(geometry_probe PRIVATE puzzle_session puzzle_geometry)
    add_test(NAME portable_fixtures COMMAND twisty verify-fixtures --json)
    set_tests_properties(portable_fixtures PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  endif()
endif()
