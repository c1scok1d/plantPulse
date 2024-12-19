#ifndef MAIN_H_
#define MAIN_H_
#include <stdint.h>

typedef struct
{
    uint8_t isProvisioned;
    char ssid[32];
    char password[64];
    // Define variables to store the sensor name and location
    char name[32];
    char location[32];
    uint8_t credentials_recv;
    char hostname[13];

    float battery_data;
} main_struct_t;

extern main_struct_t main_struct;
void ble_app_advertise(void);


#endif