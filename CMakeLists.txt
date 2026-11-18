target_sources(app PRIVATE src/c4mtb_cursor_speed.c)

if(CONFIG_C4MTB_TELEMETRY)
  zephyr_library()
  zephyr_library_sources(src/c4mtb_telemetry.c)
  # ZMK keeps the input-processor driver API in app/include in the 4.1 tree.
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_ld_options(-Wl,--wrap=raise_zmk_position_state_changed)
  zephyr_ld_options(-Wl,--wrap=raise_zmk_layer_state_changed)
endif()
