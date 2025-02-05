#ifndef MAIN_H_
#define MAIN_H_
#include <stdint.h>

typedef struct __attribute__((aligned(4))) {
    uint8_t isProvisioned;
    char ssid[32];
    char password[64];
    char name[32];
    char location[64];
    uint8_t credentials_recv;
    char hostname[32];
    char apiToken[64];
    float battery_data;
} main_struct_t;


typedef enum {
    ONE_MIN_SLEEP = 60,             // 1 minute
    FIVE_MIN_SLEEP = 300,           // 5 minutes
    TEN_MINUTE_SLEEP = 600,         // 10 minutes
    ONE_HOUR_SLEEP = 3600,          // 1 hour
    EIGHT_HOUR_SLEEP = 28800,       // 8 hours
    TWELEVE_HOUR_SLEEP = 43200,     //12 hours
} SleepDuration;  // Enum for sleep duration options

extern main_struct_t main_struct;

void ble_advert(void);
void enter_deep_sleep(SleepDuration duration);


#endif