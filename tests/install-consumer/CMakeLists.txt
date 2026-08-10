cmake_minimum_required(VERSION 3.25)

project(AgenticCircuitInstallConsumer VERSION 0.1.0 LANGUAGES C CXX)

# This consumer configures against the installed AgenticCircuit package only.
# It must not see source-tree or build-tree include paths; CI verifies that
# CMAKE_CURRENT_SOURCE_DIR stays this directory.
find_package(AgenticCircuit 0.1.0 EXACT CONFIG REQUIRED)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(process-state-plan-consumer main.cpp)
target_compile_features(process-state-plan-consumer PRIVATE cxx_std_20)
set_target_properties(process-state-plan-consumer PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(
  process-state-plan-consumer
  PRIVATE
    AgenticCircuit::ACIRAnalysis
    MLIRParser
)
