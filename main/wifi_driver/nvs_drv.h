#ifndef NVS_DRV_
#define NVS_DRV_

#include <esp_err.h>
#include <stdint.h>
esp_err_t read_from_nvs(char *ssid, char *password, char *name, char *location, char *apiToken, uint8_t *value);
esp_err_t save_to_nvs(const char *ssid, const char *password, char *name, char *location, char *apiToken, uint8_t value);

// Deep-sleep interval (seconds), stored in NVS so it's configurable at provisioning
// time instead of being a compile-time comment toggle. Defaults to 8 h if unset.
#define DEFAULT_SLEEP_SECONDS 28800u
uint32_t nvs_get_sleep_seconds(void);
esp_err_t nvs_set_sleep_seconds(uint32_t seconds);

#endif