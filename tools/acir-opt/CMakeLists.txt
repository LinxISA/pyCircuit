add_executable(acir-opt acir-opt.cpp)
target_link_libraries(acir-opt PRIVATE
  AgenticCircuit::ProjectOptions
  ACIRDialect
  ACSimDialect
  MLIRIndexDialect
  MLIRSCFDialect
  MLIROptLib
)
