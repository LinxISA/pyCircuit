add_executable(pyc_cpp_tb_fifo
  tb_fifo.cpp
)
target_include_directories(pyc_cpp_tb_fifo PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
)
target_compile_features(pyc_cpp_tb_fifo PRIVATE cxx_std_17)

add_executable(pyc_cpp_tb_issue_queue_2picker
  tb_issue_queue_2picker.cpp
)
target_include_directories(pyc_cpp_tb_issue_queue_2picker PRIVATE
  "${PROJECT_SOURCE_DIR}/include"
  "${PROJECT_SOURCE_DIR}/examples/generated/issue_queue_2picker"
)
target_compile_features(pyc_cpp_tb_issue_queue_2picker PRIVATE cxx_std_17)
