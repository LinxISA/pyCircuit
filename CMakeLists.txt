cmake_minimum_required(VERSION 3.25)

project(AgenticCircuit VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(cmake/LLVMToolchain.cmake)
acir_configure_llvm()

list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}" "${MLIR_DIR}")
include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(AddACIR)
include(CTest)

if(BUILD_TESTING)
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
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_BINARY_DIR}/include"
)
add_definitions(${LLVM_DEFINITIONS})

option(ACIR_ENABLE_ASSERTIONS "Keep C/C++ assertions enabled" OFF)

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
install(
  DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include/acir"
  DESTINATION include
)

add_subdirectory(include)
add_subdirectory(lib)
add_subdirectory(tools)
if(BUILD_TESTING)
  add_subdirectory(test)
  add_subdirectory(unittests)
endif()
