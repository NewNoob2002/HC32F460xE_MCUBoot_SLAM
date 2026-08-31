add_library(easylogger_port_hc32 STATIC
    src/elog.c
    src/elog_utils.c
    port/elog_port.c
)
target_include_directories(easylogger_port_hc32 PUBLIC inc port)
target_link_libraries(easylogger_port_hc32 PUBLIC hc32_platform hc32_project_options)
