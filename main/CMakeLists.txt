idf_component_register(SRCS
    "main.c"
    "wifi_manager.c"
    "web_server.c"
    "coin_acceptor.c"
    "anti_abuse.c"
    "rate_config.c"
    "mikrotik_api.c"
    "config_manager.c"
    INCLUDE_DIRS "."
)

# Create SPIFFS image from web_interface directory
set(WEB_INTERFACE_DIR ${CMAKE_SOURCE_DIR}/web_interface)
set(SPIFFS_IMAGE_DIR ${CMAKE_BINARY_DIR}/spiffs_image)

# Create directory for SPIFFS files
file(MAKE_DIRECTORY ${SPIFFS_IMAGE_DIR})

# Copy web files to staging directory
if(EXISTS ${WEB_INTERFACE_DIR})
    file(GLOB_RECURSE WEB_FILES "${WEB_INTERFACE_DIR}/*")
    
    foreach(FILE ${WEB_FILES})
        get_filename_component(FILE_NAME ${FILE} NAME)
        configure_file(${FILE} ${SPIFFS_IMAGE_DIR}/${FILE_NAME} COPYONLY)
    endforeach()
    
    message(STATUS "SPIFFS files copied to ${SPIFFS_IMAGE_DIR}")
    
    # Create SPIFFS image
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/spiffs.bin
        COMMAND python3 ${IDF_PATH}/components/spiffs/spiffsgen.py
            0x10000
            ${SPIFFS_IMAGE_DIR}
            ${CMAKE_BINARY_DIR}/spiffs.bin
        DEPENDS ${WEB_FILES}
        COMMENT "Creating SPIFFS image"
        VERBATIM
    )
    
    add_custom_target(spiffs_image ALL
        DEPENDS ${CMAKE_BINARY_DIR}/spiffs.bin
    )
endif()
