add_executable(acir-opt acir-opt.cpp)
set(ACIR_OPT_LIBRARIES
    AgenticCircuit::ProjectOptions ACIRAnalysis ACIRTransforms ACIRDialect
    ACSimDialect MLIRFuncDialect MLIRIndexDialect MLIRSCFDialect
    MLIRTransforms MLIROptLib)
target_link_libraries(acir-opt PRIVATE ${ACIR_OPT_LIBRARIES})

if(BUILD_TESTING)
  add_executable(acir-opt-internal acir-opt.cpp)
  target_compile_definitions(acir-opt-internal PRIVATE ACIR_INTERNAL_TEST_TOOL=1)
  target_link_libraries(acir-opt-internal PRIVATE ${ACIR_OPT_LIBRARIES})
endif()
