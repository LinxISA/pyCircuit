find_package(GTest CONFIG REQUIRED)

add_executable(ACIRTypesTests TypesTest.cpp)
target_link_libraries(ACIRTypesTests PRIVATE ACIRDialect MLIRIR GTest::gtest_main)
add_test(NAME ACIRTypesTests COMMAND ACIRTypesTests)
