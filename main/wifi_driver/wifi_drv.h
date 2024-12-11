#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

// Function to initialize Wi-Fi
void wifi_init(void);

// Function to connect to Wi-Fi using stored credentials
esp_err_t wifi_connect();

#endif // WIFI_H
