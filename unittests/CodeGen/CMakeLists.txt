find_package(GTest CONFIG REQUIRED)
add_executable(CodeGenTests
  CodeGenTest.cpp
  ModelPlanTest.cpp
)
target_include_directories(CodeGenTests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(CodeGenTests PRIVATE
  ACIRCodeGen
  MLIRIndexDialect
  MLIRParser
  GTest::gtest_main
)
target_compile_definitions(CodeGenTests PRIVATE
  ACSIM_VALID_TEST_FILE="${PROJECT_SOURCE_DIR}/test/ACSim/ops-valid.mlir"
)
add_test(NAME CodeGenTests COMMAND CodeGenTests)
