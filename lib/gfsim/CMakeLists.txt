add_library(gfsim STATIC
  harness.cpp
  system.cpp
  trace.cpp
)
target_include_directories(gfsim PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(gfsim PUBLIC cxx_std_20)
target_link_libraries(gfsim PUBLIC ACIRBindings)
