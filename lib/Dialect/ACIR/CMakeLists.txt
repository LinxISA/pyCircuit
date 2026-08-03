add_acir_library(ACIRDialect SOURCES ACIRDialect.cpp)
add_dependencies(ACIRDialect ACIRDialectIncGen)
target_include_directories(ACIRDialect PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
add_library(AgenticCircuit::ACIRDialect ALIAS ACIRDialect)
