add_acir_library(ACSimDialect SOURCES
  ACSimDialect.cpp
  ACSimOps.cpp
  ACSimTypes.cpp
)
add_dependencies(ACSimDialect
  ACSimDialectIncGen
  ACSimOpsIncGen
  ACSimTypesIncGen
)
target_link_libraries(ACSimDialect PUBLIC
  MLIRSideEffectInterfaces
)
target_include_directories(ACSimDialect PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
add_library(AgenticCircuit::ACSimDialect ALIAS ACSimDialect)
