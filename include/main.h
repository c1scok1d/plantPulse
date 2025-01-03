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
    char apiToken[128];
    float battery_data;
} main_struct_t;

typedef enum {
    TEN_MINUTE_SLEEP = 10,      // 10 seconds
    ONE_MIN_SLEEP = 60,     // 1 minute
    FIVE_MIN_SLEEP = 300,  // 5 minutes
    ONE_HOUR_SLEEP = 3600,     // 1 hour
    EIGHT_HOUR_SLEEP = 28800  // 8 hours
} SleepDuration;  // Enum for sleep duration options

extern main_struct_t main_struct;

void ble_advert(void);
void enter_deep_sleep(SleepDuration duration);


#endif