Python3_add_library(agentic_circuit_native MODULE NativeModule.cpp)
target_compile_definitions(agentic_circuit_native PRIVATE Py_LIMITED_API=0x030B0000)
target_link_libraries(agentic_circuit_native PRIVATE
  ACIRCompiler
  Python3::Module
)
set_target_properties(agentic_circuit_native PROPERTIES
  OUTPUT_NAME "_native"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/python/agentic_circuit"
)

install(
  TARGETS agentic_circuit_native
  LIBRARY DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit"
  RUNTIME DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit"
)
