include(FindPackageHandleStandardArgs)

find_path(Brotli_INCLUDE_DIR
  NAMES brotli/decode.h
)

find_library(Brotli_DEC_LIBRARY
  NAMES brotlidec
)

find_library(Brotli_COMMON_LIBRARY
  NAMES brotlicommon
)

find_package_handle_standard_args(
  Brotli
  REQUIRED_VARS
    Brotli_INCLUDE_DIR
    Brotli_DEC_LIBRARY
    Brotli_COMMON_LIBRARY
)

if(Brotli_FOUND)
  if(NOT TARGET Brotli::decoder)
    add_library(Brotli::decoder UNKNOWN IMPORTED)
    set_target_properties(Brotli::decoder PROPERTIES
      IMPORTED_LOCATION "${Brotli_DEC_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${Brotli_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "${Brotli_COMMON_LIBRARY}"
    )
  endif()
endif()

