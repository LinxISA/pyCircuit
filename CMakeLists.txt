cmake_minimum_required(VERSION 3.25)

project(AgenticCircuit VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(cmake/LLVMToolchain.cmake)
acir_configure_llvm()

function(acir_append_llvm_link_flag flag)
  set_property(GLOBAL APPEND PROPERTY ACIR_LLVM_LINK_FLAGS_PROPERTY "${flag}")
endfunction()

function(acir_collect_llvm_link_target target)
  get_property(_visited GLOBAL PROPERTY ACIR_LLVM_LINK_TARGETS_PROPERTY)
  if("${target}" IN_LIST _visited)
    return()
  endif()
  set_property(GLOBAL APPEND PROPERTY ACIR_LLVM_LINK_TARGETS_PROPERTY "${target}")

  get_target_property(_location "${target}" IMPORTED_LOCATION)
  if(NOT _location OR _location MATCHES "-NOTFOUND$")
    get_target_property(_configurations "${target}" IMPORTED_CONFIGURATIONS)
    foreach(_configuration IN LISTS _configurations)
      string(TOUPPER "${_configuration}" _configuration_upper)
      get_target_property(
        _candidate "${target}" "IMPORTED_LOCATION_${_configuration_upper}"
      )
      if(_candidate AND NOT _candidate MATCHES "-NOTFOUND$")
        set(_location "${_candidate}")
        break()
      endif()
    endforeach()
  endif()
  if(_location AND NOT _location MATCHES "-NOTFOUND$")
    acir_append_llvm_link_flag("${_location}")
  endif()

  get_target_property(_dependencies "${target}" INTERFACE_LINK_LIBRARIES)
  if(NOT _dependencies OR _dependencies MATCHES "-NOTFOUND$")
    return()
  endif()
  foreach(_dependency IN LISTS _dependencies)
    if(_dependency MATCHES "^\\$<LINK_ONLY:([^>]+)>$")
      set(_dependency "${CMAKE_MATCH_1}")
    elseif(_dependency MATCHES "^\\$<")
      message(FATAL_ERROR
        "unsupported LLVM link dependency expression: ${_dependency}"
      )
    endif()
    if(TARGET "${_dependency}")
      acir_collect_llvm_link_target("${_dependency}")
    elseif(IS_ABSOLUTE "${_dependency}" OR _dependency MATCHES "^-")
      acir_append_llvm_link_flag("${_dependency}")
    else()
      acir_append_llvm_link_flag("-l${_dependency}")
    endif()
  endforeach()
endfunction()

set_property(GLOBAL PROPERTY ACIR_LLVM_LINK_FLAGS_PROPERTY "")
set_property(GLOBAL PROPERTY ACIR_LLVM_LINK_TARGETS_PROPERTY "")
if(TARGET LLVM)
  acir_collect_llvm_link_target(LLVM)
else()
  acir_collect_llvm_link_target(LLVMSupport)
endif()
get_property(ACIR_LLVM_LINK_FLAGS GLOBAL PROPERTY ACIR_LLVM_LINK_FLAGS_PROPERTY)

list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}" "${MLIR_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(AddACIR)
include(CTest)

if(BUILD_TESTING)
  find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
  find_program(ACIR_LIT_EXECUTABLE NAMES llvm-lit lit
    HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin"
    REQUIRED
  )
  set(LLVM_EXTERNAL_LIT "${ACIR_LIT_EXECUTABLE}" CACHE FILEPATH
    "lit executable used by Agentic Circuit tests" FORCE
  )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include"
)
add_definitions(${LLVM_DEFINITIONS})

option(ACIR_ENABLE_ASSERTIONS "Keep C/C++ assertions enabled" OFF)
option(
  ACIR_BUILD_REFERENCE_MODELS
  "Build non-installed reference models used as code-generation baselines"
  ${BUILD_TESTING}
)

add_library(acir_project_options INTERFACE)
add_library(AgenticCircuit::ProjectOptions ALIAS acir_project_options)
set_target_properties(acir_project_options PROPERTIES EXPORT_NAME ProjectOptions)
target_compile_features(acir_project_options INTERFACE cxx_std_20)
if(ACIR_ENABLE_ASSERTIONS)
  if(MSVC)
    target_compile_options(acir_project_options INTERFACE /UNDEBUG)
  else()
    target_compile_options(acir_project_options INTERFACE -UNDEBUG)
  endif()
endif()

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/AgenticCircuitConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/AgenticCircuitConfig.cmake"
  INSTALL_DESTINATION lib/cmake/AgenticCircuit
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/AgenticCircuitConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY ExactVersion
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/AgenticCircuitConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/AgenticCircuitConfigVersion.cmake"
  DESTINATION lib/cmake/AgenticCircuit
)
install(TARGETS acir_project_options EXPORT AgenticCircuitTargets)
install(
  EXPORT AgenticCircuitTargets
  FILE AgenticCircuitTargets.cmake
  NAMESPACE AgenticCircuit::
  DESTINATION lib/cmake/AgenticCircuit
)
install(FILES cmake/modules/AddACIR.cmake DESTINATION lib/cmake/AgenticCircuit/modules)
install(DIRECTORY include/acir DESTINATION include)
install(DIRECTORY include/gfsim DESTINATION include)
install(
  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/acir"
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h.inc"
)

# The Python bridge locates the same installed-style runtime surface in the
# build tree, keeping native compilation independent of source-tree paths.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/include/gfsim"
  DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/include")
add_custom_target(acir_runtime_headers
  COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gfsim"
    "${CMAKE_CURRENT_BINARY_DIR}/include/gfsim"
  COMMENT "Synchronizing gfsim runtime headers"
)

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(python)
add_subdirectory(tools)
if(ACIR_BUILD_REFERENCE_MODELS)
  add_subdirectory(examples/reference/davincioo-gfsim)
endif()
if(BUILD_TESTING)
  add_subdirectory(test)
  add_subdirectory(unittests)
  add_test(
    NAME Phase5E2ETests
    COMMAND "${Python3_EXECUTABLE}" -m unittest tests.e2e.test_phase5_examples -v
  )
  set_tests_properties(Phase5E2ETests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    TIMEOUT 300
    LABELS "e2e;phase5"
    ENVIRONMENT "AGENTIC_CIRCUIT_TEST_BUILD_DIR=${CMAKE_BINARY_DIR}"
  )
endif()
