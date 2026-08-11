find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter Development.Module)

set(
  ACIR_PYTHON_INSTALL_DIR
  "lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages"
  CACHE STRING
  "Install destination for the Agentic Circuit Python package"
)

add_subdirectory(native)

install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/src/agentic_circuit"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}"
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)
install(
  DIRECTORY "${PROJECT_SOURCE_DIR}/schemas"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit/_data"
)
install(
  FILES "${PROJECT_SOURCE_DIR}/resources/diagnostics-v0.1.json"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit/_data"
)
