set(PYC_RUNTIME_PUBLIC_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

add_library(pyc4_runtime STATIC
  pyc_runtime.cpp
)
target_compile_features(pyc4_runtime PUBLIC cxx_std_17)
set_target_properties(pyc4_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  EXPORT_NAME pyc4_runtime
)
target_include_directories(pyc4_runtime
  PUBLIC
    "$<BUILD_INTERFACE:${PYC_RUNTIME_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

if(PYC_RUNTIME_ENABLE_ZLIB_TRACE)
  find_package(ZLIB REQUIRED)
  target_compile_definitions(pyc4_runtime PUBLIC PYC_RUNTIME_ENABLE_ZLIB_TRACE=1)
  target_link_libraries(pyc4_runtime PUBLIC ZLIB::ZLIB)
endif()

if(PYC_RUNTIME_BUILD_SHARED)
  add_library(pyc4_runtime_shared SHARED
    pyc_runtime.cpp
  )
  set_target_properties(pyc4_runtime_shared PROPERTIES
    OUTPUT_NAME pyc4_runtime
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    INSTALL_RPATH "${PYC_INSTALL_RPATH}"
    BUILD_WITH_INSTALL_RPATH OFF
  )
  target_compile_features(pyc4_runtime_shared PUBLIC cxx_std_17)
  target_include_directories(pyc4_runtime_shared
    PUBLIC
      "$<BUILD_INTERFACE:${PYC_RUNTIME_PUBLIC_INCLUDE_DIR}>"
      "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  )
  if(PYC_RUNTIME_ENABLE_ZLIB_TRACE)
    target_compile_definitions(pyc4_runtime_shared PUBLIC PYC_RUNTIME_ENABLE_ZLIB_TRACE=1)
    target_link_libraries(pyc4_runtime_shared PUBLIC ZLIB::ZLIB)
  endif()
endif()
