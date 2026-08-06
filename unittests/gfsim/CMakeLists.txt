find_package(GTest CONFIG REQUIRED)
add_executable(GfsimTests
  core_test.cpp
)
target_include_directories(GfsimTests PRIVATE
  ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GfsimTests PRIVATE
  gfsim
  GTest::gtest_main
)
add_test(NAME GfsimTests COMMAND GfsimTests)
