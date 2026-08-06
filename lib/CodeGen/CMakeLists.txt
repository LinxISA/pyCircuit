add_acir_library(ACIRCodeGen SOURCES
  Manifest.cpp
  Emitter.cpp
)
target_link_libraries(ACIRCodeGen PUBLIC
  ACIRDialect
  LLVMSupport
)
