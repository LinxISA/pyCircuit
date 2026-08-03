cmake_minimum_required(VERSION 3.25)

project(AgenticCircuit VERSION 0.1.0 LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
include(cmake/LLVMToolchain.cmake)
acir_configure_llvm()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
  COMPATIBILITY SameMajorVersion
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
