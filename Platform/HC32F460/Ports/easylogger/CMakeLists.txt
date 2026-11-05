set(EASYLOGGER_SOURCES
    src/elog.c
    src/elog_utils.c
    port/elog_port.c
)

add_library(easylogger_port_hc32 STATIC ${EASYLOGGER_SOURCES})
hc32_apply_project_options(easylogger_port_hc32)
set_source_files_properties(src/elog.c src/elog_utils.c PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(easylogger_port_hc32 SYSTEM PUBLIC inc port)
target_link_libraries(easylogger_port_hc32 PUBLIC hc32_platform hc32_project_options segger_rtt)
