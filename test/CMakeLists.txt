get_filename_component(ACIR_TEST_LLVM_LIB_DIR "${LLVM_DIR}/../.." ABSOLUTE)

configure_lit_site_cfg(
  "${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py"
  MAIN_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py"
)

add_lit_testsuite(check-acir "Running Agentic Circuit lit tests"
  "${CMAKE_CURRENT_BINARY_DIR}"
  DEPENDS acir-cxxgen acir-opt acir-opt-internal
)
