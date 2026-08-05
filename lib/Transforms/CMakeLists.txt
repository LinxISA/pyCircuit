add_acir_library(ACIRTransforms SOURCES
  VerifyModel.cpp
  FreezeTopology.cpp
  CanonicalizeModel.cpp
  ResolveBindings.cpp
  NormalizeACIRFile.cpp
  VerifyACIRFile.cpp
)
add_dependencies(ACIRTransforms ACIRTransformsIncGen)
target_link_libraries(ACIRTransforms PUBLIC
  ACIRAnalysis
  ACIRBindings
  ACIRDialect
  MLIRPass
)
target_include_directories(ACIRTransforms PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
)
target_include_directories(ACIRTransforms PRIVATE
  "${PROJECT_SOURCE_DIR}/lib"
)
add_library(AgenticCircuit::ACIRTransforms ALIAS ACIRTransforms)
