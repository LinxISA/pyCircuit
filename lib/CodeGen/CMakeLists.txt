add_acir_library(ACIRCodeGen SOURCES
  Build.cpp
  CompilePlan.cpp
  Manifest.cpp
  Emitter.cpp
  Generator.cpp
  ModelPlan.cpp
  ModelPlanDetails.cpp
  ProcessGenerator.cpp
)
target_link_libraries(ACIRCodeGen PUBLIC
  ACIRBindings
  ACIRDialect
  ACSimDialect
  LLVMSupport
)
target_include_directories(ACIRCodeGen PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
add_library(AgenticCircuit::ACIRCodeGen ALIAS ACIRCodeGen)
