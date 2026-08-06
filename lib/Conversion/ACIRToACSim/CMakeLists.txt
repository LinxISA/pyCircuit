add_acir_library(ACIRToACSim SOURCES
  ACIRToACSim.cpp
  TypeConverter.cpp
)
add_dependencies(ACIRToACSim ACIRTransformsIncGen ACIRDialectIncGen)
target_link_libraries(ACIRToACSim PUBLIC
  ACIRAnalysis
  ACIRBindings
  ACIRTransforms
  ACIRDialect
  ACSimDialect
  MLIRPass
  MLIRTransformUtils
)
target_include_directories(ACIRToACSim PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
target_include_directories(ACIRToACSim PRIVATE
  "${PROJECT_SOURCE_DIR}/lib"
)
add_library(AgenticCircuit::ACIRToACSim ALIAS ACIRToACSim)
