find_package(GTest CONFIG REQUIRED)
add_executable(CodeGenTests
  CodeGenTest.cpp
)
target_include_directories(CodeGenTests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(CodeGenTests PRIVATE
  ACIRCodeGen
  GTest::gtest_main
)
add_test(NAME CodeGenTests COMMAND CodeGenTests)
