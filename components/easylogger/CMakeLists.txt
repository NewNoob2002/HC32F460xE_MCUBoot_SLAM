add_library(hc32_easylogger STATIC
    src/elog.c
    src/elog_utils.c
    port/elog_port.c
)
target_include_directories(hc32_easylogger PUBLIC inc port)
target_link_libraries(hc32_easylogger PUBLIC hc32_platform hc32_project_options)
