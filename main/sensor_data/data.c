#include "data.h"
#include "driver/adc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "rest_methods.h"
#include "time.h"      // For time manipulation (including time-related functions like local time)
#include "sntp.h" 
#include <stdio.h>
#include "driver/i2c.h"

static const char *TAG = "DATA";

// Map function (equivalent to Arduino's map function)
int map(int x, int in_min, int in_max, int out_min, int out_max) {
    if (in_min == in_max) return out_min; // Avoid division by zero
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


#define I2C_MASTER_SCL_IO  17    // SCL pin
#define I2C_MASTER_SDA_IO  16    // SDA pin
#define I2C_MASTER_FREQ_HZ 400000 // I2C frequency
#define I2C_MASTER_NUM I2C_NUM_0 // I2C port number

#define MAX17048_ADDR  0x36  // MAX17048 I2C address
#define REG_VCELL      0x02  // Voltage register
#define REG_SOC        0x04  // SOC register
#define REG_STATUS     0x1A  // Status register
#define REG_CRATE      0X16

// Function to initialize I2C
static void i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ
    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

// Function to read a 16-bit register from MAX17048
static uint16_t max17048_read_register(uint8_t reg) {
    uint8_t data[2] = {0};  // Data buffer to hold 2 bytes
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();  // Create I2C command handle

    // Start the I2C communication
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_WRITE, true);  // Write address with write flag
    i2c_master_write_byte(cmd, reg, true);  // Specify the register to read

    // Restart to switch to I2C read mode
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_READ, true);  // Switch to read mode
    i2c_master_read_byte(cmd, &data[0], I2C_MASTER_ACK);  // Read the first byte (MSB)
    i2c_master_read_byte(cmd, &data[1], I2C_MASTER_NACK);  // Read the second byte (LSB)
    
    // End the I2C communication
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);  // Execute the I2C command
    i2c_cmd_link_delete(cmd);  // Delete the I2C command handle to free resources

    // Combine the two bytes to form the 16-bit result
    return (data[0] << 8) | data[1];  // Return as 16-bit value (MSB first)
}

// Function to read from a register on the MAX17048
esp_err_t read_register(uint8_t reg_addr, uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_ACK);
    i2c_master_stop(cmd);
    i2c_cmd_link_delete(cmd);
    return ESP_OK;
}
// structure to store battery data
struct BatteryStatus {
    float soc;
    bool status;
    float crate;
};

BatteryStatus getBattery() {
    static const char *TAG = "BATTERY";  // Logging tag
    BatteryStatus batteryStatus = { -1.0, false }; // Default values

    uint8_t data[2];
    uint16_t voltage, soc, crate, state;

    // Read VCELL register (battery voltage)
    uint16_t vcell_raw = max17048_read_register(REG_VCELL);
    float voltage_actual = (vcell_raw >> 4) * 1.25 / 1000; // Convert to volts

    // Read SOC register (State of Charge)
    uint16_t soc_raw = max17048_read_register(REG_SOC);
    float soc_actual = soc_raw / 256.0; // Convert to percentage
    batteryStatus.soc = soc_actual ;

    // Read CRATE register (Charge/Discharge rate)
    uint16_t crate_raw = max17048_read_register(REG_CRATE);
    float crate_actual = crate_raw / 256.0; // Convert to percentage
    batteryStatus.crate = crate_actual;

    // Read STATE register (Battery state)
    ESP_ERROR_CHECK(read_register(REG_STATUS, data, 2));  // STATE address
    uint16_t status_raw = max17048_read_register(REG_STATUS);
    state = (data[0] << 8) | data[1];  // Raw state value
    //bool charging = !(state & 0x01);  // If bit 0 is 0, charging is true
    batteryStatus.status = !(state & 0x01);  // If bit 0 is 0, charging is true

     // Log the data
    //ESP_LOGI(TAG, "Battery Monitoring Data:");
    ESP_LOGI(TAG, "Voltage: %.3f V", voltage_actual); // Convert to volts;  // Log voltage in volts (real_voltage in mV)
    ESP_LOGI(TAG, "State of Charge (SOC): %.2f%%", batteryStatus.soc);
    ESP_LOGI(TAG, "Charge/Discharge Rate: %.2f%%/hr", batteryStatus.crate);
    //ESP_LOGI(TAG, "Battery State: %s", (batteryStatus.status) ? "Discharging" : "Charging");

    return batteryStatus;
}

// Function to read moisture level
int readMoisture() {
    static const char *TAG = "MOISTURE";  // Logging tag
    int reading = 0;


    // Read ADC1 channel 5 (GPIO5)
    reading = adc1_get_raw(ADC1_CHANNEL_4);  // Get the raw ADC value

    // Map the ADC raw value to a percentage (0-100)
    int moisture = map(reading, 3600, 2130, 0, 100);

    // Log the raw ADC reading and calculated moisture percentage
    ESP_LOGI(TAG, "Raw ADC Reading: %d, Moisture %%: %d%%", reading, moisture);

    return moisture;
}

// Function to send data to the server in a FreeRTOS task
void uploadReadingsTask(void *param)
{
    // Cast the parameter back to the expected data structure
    typedef struct {
        int moisture;
        float battery;
        bool battery_status;
        const char* hostname;
        const char* sensorName;
        const char* sensorLocation;
        const char* apiToken;
    } upload_data_t;
    
    upload_data_t *data = (upload_data_t *)param;

    const char *serverName = "http://athome.rodlandfarms.com";
    const char *server_path = "/api/esp/data?";
    //const char* apiToken = "gS6gy56jcnE4Bh9ffcOgbsv8RbKuUQZqRrDCxuBu7ck2Moakxj0SJXH59ye0";

    // Clamp the moisture and battery value to the range [0, 100]
    if (data->moisture > 100) {
        data->moisture = 100;
    } 
    if (data->battery > 100) {
        data->battery = 100;
    }

    // Construct the server URI
    char server_uri[256];
    snprintf(server_uri, sizeof(server_uri), "%s%s", serverName, server_path);

    // Construct the HTTP request data
    char httpRequestData[512];
    snprintf(httpRequestData, sizeof(httpRequestData),
             "api_token=%s&hostname=%s&sensor=%s&location=%s&moisture=%d&batt=%.2f&battery_status=%d",
             data->apiToken, data->hostname, data->sensorName, data->sensorLocation, data->moisture, data->battery, data->battery_status);

    // Send the POST request
    int httpResponseCode = POST(server_uri, httpRequestData);

    // Check if the request was successful
    if (httpResponseCode == 200) {
        ESP_LOGI("UploadReadingsTask", "POST request successful. HTTP response code: %d", httpResponseCode);
    } else {
        ESP_LOGE("UploadReadingsTask", "POST request failed. HTTP response code: %d", httpResponseCode);
    }

    // Free the allocated memory for task parameter
    free(data);

    // Delete the task once done
    vTaskDelete(NULL);
}

void uploadReadings(int moisture, float battery, bool battery_status, const char* hostname, const char* sensorName, const char* sensorLocation, const char* apiToken)
{
    // Create a structure to pass the data to the task
    typedef struct {
        int moisture;
        float battery;
        bool battery_status;
        const char* hostname;
        const char* sensorName;
        const char* sensorLocation;
        const char* apiToken;
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
    data->battery_status = battery_status;
    data->hostname = hostname;
    data->sensorName = sensorName;
    data->sensorLocation = sensorLocation;
    data->apiToken = apiToken;

    // Create a FreeRTOS task to upload the readings
    xTaskCreate(uploadReadingsTask, "UploadReadingsTask", 8192, data, 5, NULL);
}

void monitor(){
    // Check for firmwareupdate
    check_update();
    //xTaskCreate(check_update, "check_update", 8192, NULL, 5, NULL);
    
    i2c_master_init();

    // Read battery and moisture data
    BatteryStatus battery = getBattery();
    int moisture = readMoisture();

    // Upload data
    uploadReadings(moisture, battery.soc, battery.status, main_struct.hostname, main_struct.name, main_struct.location, main_struct.apiToken);

    // Delay for response from server
    vTaskDelay(pdMS_TO_TICKS(3000));  
    
    // Sleep for 1 minute
    //enter_deep_sleep(ONE_MIN_SLEEP);

    // Sleep for 1 hour
    //enter_deep_sleep(ONE_HOUR_SLEEP);

    // Sleep for 8 hours
    enter_deep_sleep(EIGHT_HOUR_SLEEP);

    // Sleep for 12 hours
    //enter_deep_sleep(TWELEVE_HOUR_SLEEP);
}