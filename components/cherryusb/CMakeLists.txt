add_library(cherryusb_device OBJECT core/usbd_core.c)
target_include_directories(cherryusb_device PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/Config/Application
)
target_link_libraries(cherryusb_device PUBLIC hc32_project_options)
target_compile_options(cherryusb_device PRIVATE -Wno-unused-parameter)
