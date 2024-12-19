idf_component_register(SRCS "main.c" 
"wifi_driver/wifi_drv.c" 
"wifi_driver/nvs_drv.c" 
"sensor_data/data.c" 
"rest_methods/rest_methods.c"
                    INCLUDE_DIRS "." "../include" "wifi_driver" "sensor_data" "rest_methods")
