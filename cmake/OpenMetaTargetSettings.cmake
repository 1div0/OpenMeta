include_guard(GLOBAL)

function(openmeta_apply_target_settings target_name)
  target_compile_features(${target_name} PUBLIC cxx_std_20)
  target_include_directories(${target_name} PUBLIC "${PROJECT_SOURCE_DIR}/include")
  set_target_properties(${target_name} PROPERTIES CXX_EXTENSIONS OFF)

  if(MSVC)
    target_compile_options(${target_name} PRIVATE /W4 /EHs-c- /GR-)
    target_compile_definitions(${target_name} PRIVATE _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(
      ${target_name}
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Werror=return-type
        -fno-exceptions
        -fno-rtti
    )
  endif()
endfunction()

