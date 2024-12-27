#ifndef NVS_DRV_
#define NVS_DRV_

#include <esp_err.h>
esp_err_t read_from_nvs(char *ssid, char *password, char *name, char *location, char *apiToken, uint8_t *value);
esp_err_t save_to_nvs(const char *ssid, const char *password, char *name, char *location, char *apiToken, uint8_t value);

#endif