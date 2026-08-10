add_acir_library(ACIRCodeGen SOURCES
  Manifest.cpp
  Emitter.cpp
)
target_link_libraries(ACIRCodeGen PUBLIC
  ACIRDialect
  LLVMSupport
)
target_include_directories(ACIRCodeGen PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
add_library(AgenticCircuit::ACIRCodeGen ALIAS ACIRCodeGen)
