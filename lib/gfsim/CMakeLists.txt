add_library(gfsim STATIC
  harness.cpp
  observation.cpp
  showcase.cpp
  system.cpp
  trace.cpp
)
add_dependencies(gfsim acir_runtime_headers)
target_include_directories(gfsim PUBLIC
  "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_compile_features(gfsim PUBLIC cxx_std_20)
target_link_libraries(gfsim PUBLIC ACIRBindings)
set_target_properties(gfsim PROPERTIES EXPORT_NAME Gfsim)
