add_subdirectory(Dialect)
add_subdirectory(Analysis)
add_subdirectory(Bindings)
add_subdirectory(Transforms)
add_subdirectory(Conversion)
add_subdirectory(gfsim)
add_subdirectory(CodeGen)

# The installed package exposes the dialect and analysis surface required by
# the public ProcessStatePlan API. Internal pass, conversion, and runtime
# libraries stay build-tree only.
install(
  TARGETS ACIRDialect ACSimDialect ACIRBindings ACIRAnalysis
  EXPORT AgenticCircuitTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
