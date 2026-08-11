find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter Development.Module)

set(
  ACIR_PYTHON_INSTALL_DIR
  "lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages"
  CACHE STRING
  "Install destination for the Agentic Circuit Python package"
)

add_subdirectory(native)

set(
  ACIR_PYTHON_BUILD_DATA_DIR
  "${PROJECT_BINARY_DIR}/python/agentic_circuit/_data"
)
file(MAKE_DIRECTORY "${ACIR_PYTHON_BUILD_DATA_DIR}")
file(
  COPY "${PROJECT_SOURCE_DIR}/schemas"
  DESTINATION "${ACIR_PYTHON_BUILD_DATA_DIR}"
)
file(
  COPY "${PROJECT_SOURCE_DIR}/resources"
  DESTINATION "${ACIR_PYTHON_BUILD_DATA_DIR}"
)
configure_file(
  "${PROJECT_SOURCE_DIR}/python/resource-package-init.py"
  "${ACIR_PYTHON_BUILD_DATA_DIR}/schemas/__init__.py"
  COPYONLY
)
configure_file(
  "${PROJECT_SOURCE_DIR}/python/resource-package-init.py"
  "${ACIR_PYTHON_BUILD_DATA_DIR}/resources/__init__.py"
  COPYONLY
)

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
  DIRECTORY "${PROJECT_SOURCE_DIR}/resources"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit/_data"
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE
)
install(
  FILES "${PROJECT_SOURCE_DIR}/python/resource-package-init.py"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit/_data/schemas"
  RENAME "__init__.py"
)
install(
  FILES "${PROJECT_SOURCE_DIR}/python/resource-package-init.py"
  DESTINATION "${ACIR_PYTHON_INSTALL_DIR}/agentic_circuit/_data/resources"
  RENAME "__init__.py"
)
install(
  PROGRAMS "${PROJECT_SOURCE_DIR}/python/agentic-circuit"
  DESTINATION bin
)
