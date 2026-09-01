set(EASYLOGGER_SOURCES
    src/elog.c
    src/elog_utils.c
    port/elog_port.c
)

add_library(easylogger_port_hc32 STATIC ${EASYLOGGER_SOURCES})
target_include_directories(easylogger_port_hc32 PUBLIC inc port)
target_link_libraries(easylogger_port_hc32 PUBLIC hc32_platform hc32_project_options)

add_library(easylogger_port_hc32_rtt STATIC EXCLUDE_FROM_ALL ${EASYLOGGER_SOURCES})
target_include_directories(easylogger_port_hc32_rtt PUBLIC inc port)
target_compile_definitions(easylogger_port_hc32_rtt PRIVATE HC32_ELOG_RTT=1)
target_link_libraries(easylogger_port_hc32_rtt PUBLIC hc32_platform hc32_project_options segger_rtt)
