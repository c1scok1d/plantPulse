#ifndef _DATA_H_
#define _DATA_H_
#include <stdbool.h>  // Add this to use 'bool' in C


//void getBattery();
//void readMoisture();  // Updated declaration with parameter
//void battery_monitor();

// Define a typedef for BatteryStatus
typedef struct {
    float soc;
    bool batteryInserted;
} BatteryStatus;

BatteryStatus getBattery();  // Declaration of getBattery function
int readMoisture();  // Declaration of readMoisture function
void check_update();
void take_reading();  // Declaration of take_reading function
void monitor();  // Declaration of battery_monitor function

#endif
