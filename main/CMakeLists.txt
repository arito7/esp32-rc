idf_component_register(SRCS "main.c" "motor_control.c"
                       EMBED_FILES "index.html" "output.css"
                       PRIV_REQUIRES spi_flash esp_wifi esp_event nvs_flash esp_http_server esp_netif esp_driver_ledc esp_driver_gpio
                       INCLUDE_DIRS "")
