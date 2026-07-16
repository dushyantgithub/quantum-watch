idf_component_register(
    SRCS "main.cpp" "buttons.c" "away_screen.cpp" "voice_assistant.cpp" "notifications.cpp" "ancs_client.cpp" "health_app.cpp" "vokrr_state.cpp" "vokrr_network.cpp" "vokrr_os.cpp" "lcd_spi_psram_dma.c" "voice_assistant_icon.c" "health_app_icon.c" "startup_logo.c"
         "fonts/vokrr_font_jost_84.c" "fonts/vokrr_font_jost_30.c" "fonts/vokrr_font_jost_21.c" "fonts/vokrr_font_jost_15.c" "fonts/vokrr_font_mono_10.c" "fonts/vokrr_font_mono_8.c"
         "../components/brookesia_core/systems/speaker/assets/images/middle/speaker_image_middle_quick_settings_bluetooth_48_48.c"
    INCLUDE_DIRS "."
    REQUIRES bt brookesia_core espressif__esp-lib-utils esp_wifi esp_netif esp_event esp_http_client esp_driver_spi esp_hw_support json nvs_flash mbedtls spi_flash waveshare__esp32_s3_touch_amoled_2_06
    WHOLE_ARCHIVE)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=spi_device_queue_trans")

# Function to get component library
function(get_component_library component_name output_var)
    # Get the exact component name
    idf_build_get_property(build_components BUILD_COMPONENTS)
    set(TARGET_COMPONENT "")
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${component_name}" OR COMPONENT MATCHES "espressif__${component_name}")
            set(TARGET_COMPONENT ${COMPONENT})
            break()
        endif()
    endforeach()

    # Get the component library
    if(TARGET_COMPONENT STREQUAL "")
        message(FATAL_ERROR "Component '${component_name}' not found.")
    else()
        idf_component_get_property(COMPONENT_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
        set(${output_var} ${COMPONENT_LIB} PARENT_SCOPE)
    endif()
endfunction()

get_component_library("lvgl" LVGL_LIB)
target_compile_options(
    ${LVGL_LIB}
    PUBLIC
        "-Wno-attributes"
)
