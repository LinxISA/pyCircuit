cmake_minimum_required(VERSION 3.20)
project(pycircuit VERSION 0.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

option(PYC_BUILD_MLIR_TOOLS "Build MLIR-based pyc tools" ON)
option(PYC_BUILD_RUNTIME_LIB "Build pyc4 runtime library" ON)
option(PYC_RUNTIME_ENABLE_ZLIB_TRACE "Enable gzip trace output in runtime (optional)" OFF)
option(PYC_RUNTIME_BUILD_SHARED "Build shared pyc4 runtime library" OFF)
option(PYC_INSTALL_TEMPLATES "Install runtime template libraries" ON)
option(PYC_INSTALL_PYTHON "Install Python frontend sources (for binary packages)" ON)

if(PYC_BUILD_MLIR_TOOLS)
  add_subdirectory(compiler/mlir)
endif()

if(PYC_BUILD_RUNTIME_LIB)
  add_subdirectory(runtime/cpp)
endif()

if(PYC_INSTALL_TEMPLATES)
  install(DIRECTORY runtime/cpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  install(DIRECTORY runtime/verilog DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
  if(PYC_BUILD_RUNTIME_LIB)
    set(PYC_RUNTIME_INSTALL_TARGETS pyc4_runtime)
    if(PYC_RUNTIME_BUILD_SHARED)
      list(APPEND PYC_RUNTIME_INSTALL_TARGETS pyc4_runtime_shared)
    endif()
    install(TARGETS ${PYC_RUNTIME_INSTALL_TARGETS}
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
  endif()
endif()

if(PYC_INSTALL_PYTHON)
  install(
    DIRECTORY compiler/frontend/pycircuit
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pycircuit/python"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
  )
  install(FILES pyproject.toml DESTINATION "${CMAKE_INSTALL_DATADIR}/pycircuit")
endif()

install(FILES LICENSE README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/pycircuit")

# Packaging (binary tarball via CPack).
set(CPACK_PACKAGE_NAME "pycircuit" CACHE STRING "CPack package name")
set(CPACK_PACKAGE_VENDOR "pyCircuit" CACHE STRING "CPack package vendor")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "pyCircuit: Python -> PYC (MLIR) -> Verilog/C++" CACHE STRING "CPack summary")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}" CACHE STRING "CPack package version")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ" CACHE STRING "CPack generator")
include(CPack)
