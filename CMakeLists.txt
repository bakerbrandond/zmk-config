if(CONFIG_PROSPECTOR_ANTI_IDLE_INDICATOR OR CONFIG_PROSPECTOR_OUTPUT_INDICATOR OR CONFIG_PROSPECTOR_STUDIO_INDICATOR OR CONFIG_PROSPECTOR_CLOCK)
  target_sources(app PRIVATE src/prospector_indicators.c src/fonts/indicator_icons_20.c)
  if(CONFIG_PROSPECTOR_CLOCK)
    target_sources(app PRIVATE src/prospector_clock.c)
  endif()
  # Hook the Prospector status screen build (see src/prospector_indicators.c)
  zephyr_ld_options(-Wl,--wrap=zmk_widget_battery_bar_init)
endif()

if(CONFIG_NICE_VIEW_SNOW)
  target_sources(app PRIVATE src/nice_view_snow.c)
  if(CONFIG_NICE_VIEW_SNOW_STATUS)
    target_sources(app PRIVATE src/nice_view_status.c src/fonts/nv_status_icons_20.c
                               src/fonts/nv_status_digits_16.c)
  endif()
  # Hook the stock nice!view status widget (see src/nice_view_snow.c)
  zephyr_ld_options(-Wl,--wrap=zmk_widget_status_init)
endif()

if(CONFIG_REBOOT_ON_FATAL_ERROR)
  target_sources(app PRIVATE src/reboot_on_fatal.c)
endif()

if(CONFIG_STRICT_OUTPUT)
  target_sources(app PRIVATE src/strict_output.c)
  # Follow the selected output and drop reports for the other (see src/strict_output.c)
  zephyr_ld_options(
    -Wl,--wrap=zmk_endpoints_select_transport
    -Wl,--wrap=zmk_endpoints_toggle_transport
    -Wl,--wrap=zmk_usb_hid_send_keyboard_report
    -Wl,--wrap=zmk_usb_hid_send_consumer_report
    -Wl,--wrap=zmk_hog_send_keyboard_report
    -Wl,--wrap=zmk_hog_send_consumer_report)
  if(CONFIG_ZMK_POINTING)
    zephyr_ld_options(
      -Wl,--wrap=zmk_usb_hid_send_mouse_report
      -Wl,--wrap=zmk_hog_send_mouse_report)
  endif()
endif()
