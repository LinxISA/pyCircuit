add_acir_library(ACIRAnalysis SOURCES ModelAnalysis.cpp)
target_link_libraries(ACIRAnalysis PUBLIC
  ACIRDialect
  MLIRFuncDialect
  MLIRSideEffectInterfaces
  LLVMSupport
)
target_include_directories(ACIRAnalysis PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
add_library(AgenticCircuit::ACIRAnalysis ALIAS ACIRAnalysis)
