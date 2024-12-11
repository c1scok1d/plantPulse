idf_component_register(SRCS "main.c" "wifi_driver/wifi_drv.c" "wifi_driver/nvs_drv.c" "sensor_data/data.c" "wifi_driver/http.c"
                    INCLUDE_DIRS "." "../include" "wifi_driver" "sensor_data")
