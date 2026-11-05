set(HC32_PROJECT_C_WARNING_OPTIONS
    -Wall
    -Wextra
    -Wconversion
    -Wshadow
    -Wcast-align
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wundef
    -Werror
)
set(HC32_PROJECT_CXX_WARNING_OPTIONS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wcast-align
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wundef
    -Wold-style-cast
    -Wdelete-non-virtual-dtor
    -Woverloaded-virtual
    -Werror
)

# Apply one language policy to project-owned sources. Vendored targets keep the
# toolchain/runtime interface but opt out of diagnostics for their own sources.
function(hc32_apply_project_options target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:C>:${HC32_PROJECT_C_WARNING_OPTIONS}>"
        "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fno-use-cxa-atexit;${HC32_PROJECT_CXX_WARNING_OPTIONS}>"
    )
endfunction()
