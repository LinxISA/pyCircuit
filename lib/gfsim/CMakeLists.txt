add_library(gfsim STATIC
  system.cpp
)
target_include_directories(gfsim PUBLIC
  ${PROJECT_SOURCE_DIR}/include
)
target_compile_features(gfsim PUBLIC cxx_std_20)
