find_package(GTest CONFIG REQUIRED)
get_filename_component(ACIR_TEST_LLVM_LIB_DIR "${LLVM_DIR}/../.." ABSOLUTE)
set(ACIR_TEST_SANITIZER_FLAG "")
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=address")
  set(ACIR_TEST_SANITIZER_FLAG "-fsanitize=address")
elseif(CMAKE_CXX_FLAGS MATCHES "-fsanitize=undefined")
  set(ACIR_TEST_SANITIZER_FLAG "-fsanitize=undefined")
endif()
add_executable(CodeGenTests
  BuildTest.cpp
  CodeGenTest.cpp
  GeneratedModelCompileTest.cpp
  GeneratedModelRuntimeTest.cpp
  GeneratorTest.cpp
  ModelPlanTest.cpp
)
target_include_directories(CodeGenTests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/lib/CodeGen
)
target_link_libraries(CodeGenTests PRIVATE
  ACIRCodeGen
  gfsim
  MLIRIndexDialect
  MLIRParser
  GTest::gtest_main
)
target_compile_definitions(CodeGenTests PRIVATE
  ACSIM_VALID_TEST_FILE="${PROJECT_SOURCE_DIR}/test/ACSim/ops-valid.mlir"
  ACIR_TEST_BINARY_DIR="${PROJECT_BINARY_DIR}"
  ACIR_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
  ACIR_TEST_LLVM_LIB_DIR="${ACIR_TEST_LLVM_LIB_DIR}"
  ACIR_TEST_SANITIZER_FLAG="${ACIR_TEST_SANITIZER_FLAG}"
  ACIR_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_test(NAME CodeGenTests COMMAND CodeGenTests)
