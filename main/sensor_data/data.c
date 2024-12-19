#include "data.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../main.h"
#include "http.h"
#include "cJSON.h"
#include <string.h>
#include "esp_sleep.h"
#include "driver/i2c.h"
#include "rest_methods.h"
#include "time.h"      // For time manipulation (including time-related functions like local time)
#include "sntp.h" 

static const char *TAG = "DATA";
#define I2C_MASTER_SDA_IO    16      // GPI016 is SDA
#define I2C_MASTER_SCL_IO    17       // GPI017 is SCL
#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_MASTER_FREQ_HZ    100000 // I2C frequency (100 kHz)
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0


#define SLEEP_DURATION_SEC 86400 // 24 hours in seconds

// Map function (equivalent to Arduino's map function)
int map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
esp_err_t i2c_master_init() {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE("I2C", "Failed to configure I2C parameters");
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    if (err != ESP_OK) {
        ESP_LOGE("I2C", "Failed to install I2C driver");
    }
    return err;
}

#define BATTERY_I2C_ADDR 0x36  // I2C address of the battery monitoring IC
#define BATTERY_VOLTAGE_REG 0x02  // Hypothetical register address for battery voltage

esp_err_t read_battery_voltage(float *battery_voltage) {
    uint8_t data[2];  // Buffer to store the I2C data
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    // Start I2C communication
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BATTERY_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, BATTERY_VOLTAGE_REG, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BATTERY_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    // Execute the I2C command
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

   if (ret == ESP_OK) {
        // Combine the two bytes read from the register (assuming 16-bit battery voltage)
        uint16_t raw_voltage = (data[0] << 8) | data[1];
        *battery_voltage = raw_voltage * (4.2 / 4095);  // Adjust this based on your battery IC's datasheet
        
        // Log the battery voltage
        ESP_LOGI("Battery", "Battery Voltage: %.2f V", *battery_voltage);
        
        // Calculate battery percentage based on 4.2V full charge and 3.2V empty
        float battery_percentage = (*battery_voltage - 3.2) / (4.2 - 3.2) * 100;
        battery_percentage = (battery_percentage < 0) ? 0 : (battery_percentage > 100) ? 100 : battery_percentage; // Clamp to 0-100%
        
        // Log the battery voltage
        ESP_LOGI("Battery", "Battery Percentage: %.2f%%", battery_percentage);
        
        return ESP_OK;  // Return success
    } else {
        ESP_LOGE("I2C", "Failed to read battery voltage");
        return ret;  // Return the error from I2C communication
    }
}



// Function to read battery voltage
float getBattery() {
    static const char *TAG = "BatterySensor";  // Logging tag
    float battery_voltage = 0.0;

    // Call to read the battery voltage (implement this function according to your I2C setup)
    esp_err_t err = read_battery_voltage(&battery_voltage);  // Assume this function fills battery_voltage

    if (err == ESP_OK) {
        // Return the battery voltage or percentage
        // Optionally, you can return battery percentage instead of voltage
        return battery_voltage;  // This can return the battery voltage directly, or you can calculate percentage here if needed
    } else {
        // Error handling: Log the error and return a value indicating failure
        ESP_LOGE(TAG, "Error reading battery voltage: %s", esp_err_to_name(err));
        return 0.0;  // Return a failure value (e.g., 0.0 V or 0% to indicate failure)
    }
}

// Function to read moisture level
int readMoisture() {
    static const char *TAG = "MoistureSensor";  // Logging tag
    int reading = 0;


    // Read ADC1 channel 5 (GPIO5)
    reading = adc1_get_raw(ADC1_CHANNEL_4);  // Get the raw ADC value

    // Map the ADC raw value to a percentage (0-100)
    int moisture = map(reading, 4095, 0, 0, 100);

    // Log the raw ADC reading and calculated moisture percentage
    ESP_LOGI(TAG, "Raw ADC Reading: %d, Moisture: %d%%", reading, moisture);

    return moisture;


    //vTaskDelete(NULL);  // Delete task when done
}


// Replace String and std::string with const char*
void uploadReadings(int moisture, float battery, const char* hostname, const char* sensorName, const char* sensorLocation) {
    static const char *TAG = "UploadReadings";  // Logging tag

    const char *serverName = "http://athome.rodlandfarms.com";
    const char *server_path = "/api/esp/data";
    const char* apiKey = "gwGWZKjADUeHe1f06muhnhdt38pmVwBaNuiyL18WvLHLMeFUZYcqOZqsgvyl";

    // Allocate enough space to store the full URI
    size_t server_uri_len = strlen(serverName) + strlen(server_path) + 1; // +1 for the null-terminator
    char* server_uri = (char*) malloc(server_uri_len);
    if (!server_uri) {
        // Log the raw ADC reading and calculated moisture percentage
        ESP_LOGI(TAG, "Memory allocation failed for server URI\n");
        //return NULL;  // Return NULL if memory allocation fails
    }

    snprintf(server_uri, server_uri_len, "%s%s", serverName, server_path);  // Concatenate server name and path

    // Prepare the HTTP POST request data using C-style string concatenation
    // Allocate enough space for the HTTP request data
    size_t httpRequestData_len = strlen("api_token=") + strlen(apiKey) +
                                 strlen("&hostname=") + strlen(hostname) +
                                 strlen("&sensor=") + strlen(sensorName) +
                                 strlen("&location=") + strlen(sensorLocation) +
                                 strlen("&moisture=") + 12 +   // Enough space for integer + null terminator
                                 strlen("&batt=") + 20;         // Enough space for float + null terminator
    char* httpRequestData = (char*) malloc(httpRequestData_len);
    if (!httpRequestData) {
        ESP_LOGI(TAG, "Memory allocation failed for HTTP request data\n");
        free(server_uri);  // Free server_uri before returning
        //return NULL;  // Return NULL if memory allocation fails
    }

    snprintf(httpRequestData, httpRequestData_len,
             "api_token=%s&hostname=%s&sensor=%s&location=%s&moisture=%d&batt=%.2f",
             apiKey, hostname, sensorName, sensorLocation, moisture, battery);

    // Log the raw ADC reading and calculated moisture percentage
    ESP_LOGI(TAG, "%s\n\n", httpRequestData);

    // Send HTTP POST request
    //int httpResponseCode = POST(server_uri, httpRequestData);

    // Free allocated memory
    free(server_uri);
    free(httpRequestData);

    // Return the request data (if needed) or a success message
    // In C, returning the request data might not be ideal, so you could return a status code if needed.
    // Return the request data for debugging purposes
    //return httpRequestData;
}
/*
esp_err_t send_battery_data(float battery_level, int moisture)
{
    // Create a new JSON object
    cJSON *json_data = cJSON_CreateObject();

    // Add battery data to JSON object
    cJSON_AddNumberToObject(json_data, "battery", battery_level); // Add battery level
    // Add moisture data to JSON object
    cJSON_AddNumberToObject(json_data, "moisture", moisture); // Add battery level


    // Convert the JSON object to a string
    char *json_string = cJSON_Print(json_data);
    ESP_LOGI(TAG, "JSON string %s", json_string);

    if (json_string == NULL)
    {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(json_data);
        return ESP_FAIL;
    }

    // Call the HTTP POST function and pass the JSON string
    esp_err_t err = http_post(json_string, strlen(json_string));

    // Clean up the JSON object
    cJSON_Delete(json_data);
    free(json_string); // Free the allocated memory for the JSON string

    return err;
} */

void enter_deep_sleep()
{
    #define SLEEP_DURATION_SEC    30     
    uint64_t sleep_duration_us = (uint64_t)SLEEP_DURATION_SEC * (uint64_t)1000000;

    ESP_LOGI(TAG, "Entering deep sleep mode for %d seconds...", SLEEP_DURATION_SEC);

    // Configure the RTC timer to wake up in 24 hours (86400 seconds)
    esp_sleep_enable_timer_wakeup(sleep_duration_us); // Time in microseconds

    // Enter deep sleep
    esp_deep_sleep_start();
}

void monitor()
{
    int count = 0;
    esp_err_t err = i2c_master_init();

    if (err != ESP_OK) {
        ESP_LOGE("I2C", "I2C initialization failed");
        return;
    }

    while (count < 30){
        ESP_LOGI(TAG, "%d", count);

        // Read battery and moisture data
        float battery = getBattery(err);
        int moisture = readMoisture();

        // Upload data
        uploadReadings(moisture, battery, main_struct.hostname, main_struct.name, main_struct.location);

        count++;
        vTaskDelay(pdMS_TO_TICKS(5000));  // Delay for 5 seconds
    }

    // Send battery data and enter deep sleep
    enter_deep_sleep();
}

/*
void monitor()
{
    int count = 0;
    esp_err_t err = i2c_master_init();
    configTime(0, "pool.ntp.org", "0.pool.ntp.org", "1.pool.ntp.org"); // Example NTP server pool
    setenv("TZ", "America/Chicago", 1); // Set your desired timezone [2, 11]
    sntp_initialized = sntp_start(); // Start NTP client [1, 9, 11]

    struct tm timeinfo;

    if (!getLocalTime(&timeinfo)) {
        Serial.println();
        ESP_LOGI(TAG, "Failed to obtain time");
    } else {
        ESP_LOGI(TAG, "%s", asctime(&timeinfo));
    }

    if (err != ESP_OK) {
        ESP_LOGE("I2C", "I2C initialization failed");
        return;
    }

    while (count < 30){
        ESP_LOGI(TAG, "%d", count);
        float battery = getBattery(err);
        int moisture = readMoisture();
        

        uploadReadings(moisture, battery, main_struct.hostname, main_struct.name, main_struct.location);
        count ++;
        vTaskDelay(pdMS_TO_TICKS(5000));  // Delay for 5 seconds

    }

    //send_battery_data(main_struct.battery_data);
    enter_deep_sleep();
}*/