#include "data.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
//#include "http.h"
#include "cJSON.h"
#include <string.h>
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


//#define SLEEP_DURATION_SEC 86400 // 24 hours in seconds

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

// Function to send data to the server in a FreeRTOS task
void uploadReadingsTask(void *param)
{
    // Cast the parameter back to the expected data structure
    typedef struct {
        int moisture;
        float battery;
        const char* hostname;
        const char* sensorName;
        const char* sensorLocation;
    } upload_data_t;
    
    upload_data_t *data = (upload_data_t *)param;

    const char *serverName = "http://athome.rodlandfarms.com";
    const char *server_path = "/api/esp/data?";
    const char* apiKey = "ijYCm00T79FzzGNGkghmUFXLRzSQTIPaNpT01zHoWvrKhlWU5x6qyzvi9aPr";

    // Construct the server URI
    char server_uri[256];
    snprintf(server_uri, sizeof(server_uri), "%s%s", serverName, server_path);

    // Construct the HTTP request data
    char httpRequestData[512];
    snprintf(httpRequestData, sizeof(httpRequestData),
             "api_token=%s&hostname=%s&sensor=%s&location=%s&moisture=%d&batt=%.2f",
             apiKey, data->hostname, data->sensorName, data->sensorLocation, data->moisture, data->battery);

    // Send the POST request
    int httpResponseCode = POST(server_uri, httpRequestData);

    // Check if the request was successful
    if (httpResponseCode == 200) {
        ESP_LOGI("UploadReadings", "POST request successful. HTTP response code: %d", httpResponseCode);
    } else {
        ESP_LOGE("UploadReadings", "POST request failed. HTTP response code: %d", httpResponseCode);
    }

    // Free the allocated memory for task parameter
    free(data);

    // Delete the task once done
    vTaskDelete(NULL);
}

void uploadReadings(int moisture, float battery, const char* hostname, const char* sensorName, const char* sensorLocation)
{
    // Create a structure to pass the data to the task
    typedef struct {
        int moisture;
        float battery;
        const char* hostname;
        const char* sensorName;
        const char* sensorLocation;
    } upload_data_t;

    // Allocate memory for the data structure
    upload_data_t *data = (upload_data_t *)malloc(sizeof(upload_data_t));
    if (data == NULL) {
        ESP_LOGE("UploadReadings", "Failed to allocate memory for data");
        return;
    }

    // Fill the structure with the data
    data->moisture = moisture;
    data->battery = battery;
    data->hostname = hostname;
    data->sensorName = sensorName;
    data->sensorLocation = sensorLocation;

    // Create a FreeRTOS task to upload the readings
    xTaskCreate(uploadReadingsTask, "UploadReadingsTask", 8192, data, 5, NULL);
}


void monitor()
{
    //int count = 0;
    esp_err_t err = i2c_master_init();

    if (err != ESP_OK) {
        ESP_LOGE("I2C", "I2C initialization failed");
        return;
    }
    // Read battery and moisture data
    float battery = getBattery(err);
    int moisture = readMoisture();

    // Upload data
    uploadReadings(moisture, battery, main_struct.hostname, main_struct.name, main_struct.location);

    // Delay for 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));  
    
    // Sleep for 30 seconds
    enter_deep_sleep(SHORT_SLEEP);

    // Sleep for 1 hour
    //enter_deep_sleep(SLEEP_1_HOUR);

    // Sleep for 8 hours
    //enter_deep_sleep(SLEEP_8_HOURS);

    // Sleep for 24 hours
    //enter_deep_sleep(SLEEP_24_HOURS);
}