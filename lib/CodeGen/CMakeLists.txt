add_acir_library(ACIRCodeGen SOURCES
  Build.cpp
  CompilePlan.cpp
  Manifest.cpp
  Emitter.cpp
  Generator.cpp
  ModelPlan.cpp
  ModelPlanDetails.cpp
  ProcessGenerator.cpp
  QueueGraphPlan.cpp
  Staging.cpp
)
list(GET LLVM_INCLUDE_DIRS 0 ACIR_CODEGEN_LLVM_INCLUDE_DIR)
target_compile_definitions(ACIRCodeGen PRIVATE
  ACIR_CODEGEN_LLVM_INCLUDE_DIR="${ACIR_CODEGEN_LLVM_INCLUDE_DIR}"
)
target_link_libraries(ACIRCodeGen PUBLIC
  ACIRBindings
  ACIRDialect
  ACSimDialect
  MLIRArithDialect
  MLIRIndexDialect
  LLVMSupport
)
target_include_directories(ACIRCodeGen PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
add_library(AgenticCircuit::ACIRCodeGen ALIAS ACIRCodeGen)
