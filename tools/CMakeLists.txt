# Match a no-RTTI LLVM build: acir-opt subclasses MLIR passes and LLVM
# command-line options, whose vtables must not reference typeinfo symbols
# that no-RTTI LLVM libraries do not export.
if(NOT LLVM_ENABLE_RTTI)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
endif()

add_subdirectory(acir-cxxgen)
add_subdirectory(acir-queue-plan)
add_subdirectory(acir-opt)
