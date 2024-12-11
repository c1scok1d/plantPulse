#ifndef MAIN_H_
#define MAIN_H_

typedef struct
{
    uint8_t isProvisioned;
    char ssid[32];
    char password[64];
    uint8_t credentials_recv;
    char hostname[13];

    float battery_data;

    
} main_struct_t;

extern main_struct_t main_struct;


#endif