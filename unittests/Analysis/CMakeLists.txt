find_package(GTest CONFIG REQUIRED)

add_executable(ACIRModelAnalysisTests ModelAnalysisTest.cpp)
target_include_directories(ACIRModelAnalysisTests PRIVATE
  "${PROJECT_SOURCE_DIR}/lib"
)
target_link_libraries(ACIRModelAnalysisTests PRIVATE
  ACIRAnalysis
  ACIRDialect
  ACIRTransforms
  ACSimDialect
  MLIRAsmParser
  MLIRBytecodeWriter
  MLIRFuncDialect
  MLIRIndexDialect
  MLIRIR
  MLIRParser
  MLIRSCFDialect
  GTest::gtest_main
)
add_test(NAME ACIRModelAnalysisTests COMMAND ACIRModelAnalysisTests)

add_executable(ACIRProcessStatePlanTests ProcessStatePlanBasicTest.cpp)
target_include_directories(ACIRProcessStatePlanTests PRIVATE
  "${PROJECT_SOURCE_DIR}/lib"
)
target_link_libraries(ACIRProcessStatePlanTests PRIVATE
  ACIRAnalysis
  ACIRDialect
  ACIRTransforms
  ACSimDialect
  MLIRAsmParser
  MLIRBytecodeWriter
  MLIRIndexDialect
  MLIRIR
  MLIRParser
  GTest::gtest_main
)
add_test(NAME ACIRProcessStatePlanTests COMMAND ACIRProcessStatePlanTests)
