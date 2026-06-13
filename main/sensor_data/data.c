#include "data.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "nvs_drv.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "rest_methods.h"
#include "time.h"      // For time manipulation (including time-related functions like local time)
#include "sntp.h"
#include <stdio.h>
#include <math.h>     // expf() for VPD
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

    // Read CRATE register (Charge/Discharge rate). CRATE is SIGNED two's-complement:
    // positive = charging, negative = discharging; LSB = 0.208 %/hr. (Reading it as
    // unsigned/256 made discharge show as garbage like "13620 %/hr".)
    int16_t crate_raw = (int16_t)max17048_read_register(REG_CRATE);
    float crate_actual = crate_raw * 0.208f;
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

// ---- Soil-probe power gating -------------------------------------------------
// The V5 soil sensor is a TLC555 capacitive oscillator wired to the always-on
// +3V3 rail (no MOSFET / load switch on the board — confirmed against the V5
// EasyEDA schematic; see hardware/README "gate the soil-probe power rail"). So it
// oscillates 24/7 between the 8-hourly reads (~0.2–0.7 mA continuous) and is the
// dominant battery drain — gating it is a ~4–6x life win. Firmware can't switch
// what the board doesn't, so this is BLOCKED ON HARDWARE for V5.
//
// V6 adds a high-side PMOS load switch on the sensor rail, gate on GPIO21, with a
// gate pull-up so the switch is OFF when the GPIO is Hi-Z (IDF tristates GPIOs in
// deep sleep). Defaults below match that topology (GPIO21, ACTIVE-LOW). The GPIO
// is left -1 so the existing V5 fleet (no switch) stays byte-for-byte identical
// over OTA — on a V6 build just change SOIL_PWR_GPIO to GPIO_NUM_21.
#define SOIL_PWR_GPIO          GPIO_NUM_21   // V6: PMOS load-switch gate on SENSOR_3V3. (main: -1 for the V5 fleet)
#define SOIL_PWR_ACTIVE_LEVEL  (0)    // 0 = active-low (bare-PMOS high-side); 1 = active-high (NMOS low-side / direct-GPIO)
#define SOIL_PWR_SETTLE_MS     (50)   // let the TLC555 settle after power-up before sampling (tune 20–100 ms)

// Function to read moisture level
int readMoisture() {
    static const char *TAG = "MOISTURE";  // Logging tag
    int reading = 0;

#if SOIL_PWR_GPIO >= 0
    // Power the probe only for this measurement.
    gpio_reset_pin((gpio_num_t)SOIL_PWR_GPIO);
    gpio_set_direction((gpio_num_t)SOIL_PWR_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)SOIL_PWR_GPIO, SOIL_PWR_ACTIVE_LEVEL);
    vTaskDelay(pdMS_TO_TICKS(SOIL_PWR_SETTLE_MS));
#endif

    // Read ADC1 channel 5 (GPIO5)
    reading = adc1_get_raw(ADC1_CHANNEL_4);  // Get the raw ADC value

    // Map the ADC raw value to a percentage (0-100)
    int moisture = map(reading, 3600, 2130, 0, 100);

    // Log the raw ADC reading and calculated moisture percentage
    ESP_LOGI(TAG, "Raw ADC Reading: %d, Moisture %%: %d%%", reading, moisture);

#if SOIL_PWR_GPIO >= 0
    // Cut probe power. In deep sleep IDF tristates the pin; the gate pull (HW) then
    // holds the load switch OFF until the next wake.
    gpio_set_level((gpio_num_t)SOIL_PWR_GPIO, !SOIL_PWR_ACTIVE_LEVEL);
#endif

    return moisture;
}

// ---- SHT40 temperature/humidity (V6, I2C 0x44 on the existing MAX17048 bus) -----
// If an SHT40 answers at 0x44 its T/RH/VPD are appended to the upload; absent (V5)
// the upload is byte-for-byte unchanged. Powered from always-on +3V3 (see hardware
// README) so no power gating here. Units: temperature sent in °F; VPD computed in °C.
#define SHT40_ADDR         0x44
#define SHT40_CMD_MEASURE  0xFD   // high-precision single-shot (~8.3 ms)

static bool sht40_present(void) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

// Returns true and fills *temp_c / *rh on success.
static bool sht40_read(float *temp_c, float *rh) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, SHT40_CMD_MEASURE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) return false;

    vTaskDelay(pdMS_TO_TICKS(10));  // conversion time

    uint8_t d[6] = {0};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (SHT40_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, d, 5, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &d[5], I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) return false;

    uint16_t raw_t  = ((uint16_t)d[0] << 8) | d[1];   // (CRC bytes d[2], d[5] not verified)
    uint16_t raw_rh = ((uint16_t)d[3] << 8) | d[4];
    *temp_c = -45.0f + 175.0f * ((float)raw_t  / 65535.0f);
    float h = -6.0f  + 125.0f * ((float)raw_rh / 65535.0f);
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;
    *rh = h;
    return true;
}

// Vapor-pressure deficit (kPa) from temp(°C) + RH(%).
static float compute_vpd(float tc, float rh) {
    float svp = 0.6108f * expf(17.27f * tc / (tc + 237.3f));
    return svp * (1.0f - rh / 100.0f);
}

// ---- Solar-panel voltage sense (V6 optional, ADC1_CH3 / GPIO4) ------------------
// Divider 1 MΩ (to Solar+) / 820 kΩ (to GND) + 100 nF. -1 disables (no divider fitted).
// Channel attenuation is configured in app_main() alongside the soil channel.
#define SOLAR_SENSE_CH      (-1)            // V6 optional: ADC1_CHANNEL_3 (GPIO4); -1 = absent
#define SOLAR_DIV_RATIO     (2.2195f)       // (1000+820)/820 — scale ADC mV back to panel mV
#define SOLAR_PRESENT_MV    (4500)          // panel considered "producing" above this

// Sets *present/*millivolts. When SOLAR_SENSE_CH < 0, reports not-present (caller falls
// back to inferring solar from the charger STAT line).
static void read_solar(bool *present, int *millivolts) {
    *present = false; *millivolts = 0;
#if SOLAR_SENSE_CH >= 0
    adc1_config_channel_atten((adc1_channel_t)SOLAR_SENSE_CH, ADC_ATTEN_DB_11);  // width set globally in app_main
    int raw = adc1_get_raw((adc1_channel_t)SOLAR_SENSE_CH);     // 0..4095 @ 12-bit, 11 dB atten
    int adc_mv = raw * 3300 / 4095;                             // coarse (no eFuse cal) — fine for present/absent
    int panel_mv = (int)(adc_mv * SOLAR_DIV_RATIO);
    *millivolts = panel_mv;
    *present = panel_mv > SOLAR_PRESENT_MV;
#endif
}

// ---- MAX17048 low-battery alert config (V6 optional, paired with ALRT->IO18) ----
// Set ALRT_CONFIG_PCT > 0 to make ALRT (open-drain) pull low at <= that SOC%. Only useful
// when the board routes ALRT to a wake GPIO (see main.c ALRT_WAKE_GPIO). 0 = disabled.
#define ALRT_CONFIG_PCT     (0)
#if ALRT_CONFIG_PCT > 0
#define MAX17048_REG_CONFIG 0x0C
static void max17048_set_alert(uint8_t pct) {
    if (pct < 1) pct = 1; if (pct > 32) pct = 32;
    uint8_t athd = 32 - pct;                 // CONFIG[4:0] = 32 - threshold%
    uint8_t hi = 0x97;                       // RCOMP default
    uint8_t lo = athd & 0x1F;                // ALSC=0, ALRT cleared by writing 0
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MAX17048_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, MAX17048_REG_CONFIG, true);
    i2c_master_write_byte(cmd, hi, true);
    i2c_master_write_byte(cmd, lo, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
}
#endif

// Build the form-encoded body and POST it, with a small bounded retry so one flaky
// connection or server blip doesn't permanently lose an 8-hourly reading.
//
// SYNCHRONOUS: monitor() runs this in monitor_task (large stack) and only deep-sleeps
// AFTER it returns, so deep sleep can no longer cut off an in-flight upload. (The old
// design spawned a detached task and slept after a fixed 3 s delay — shorter than the
// 8 s HTTP timeout — so a slow TLS upload on weak WiFi was killed mid-flight and the
// reading was lost, which read as "device offline" in the app.) Returns true on HTTP 200.
// V6: also carries SHT40 temp/humidity/VPD + the measured-solar flag.
bool uploadReadings(int moisture, float battery, bool battery_status, float crate,
                    bool usb_present, bool charging, bool solar_present,
                    float temp_f, float humidity, float vpd, bool env_present,
                    const char* hostname, const char* sensorName,
                    const char* sensorLocation, const char* apiToken)
{
    // Clamp the moisture and battery value to the range [0, 100]
    if (moisture > 100) moisture = 100;
    if (battery  > 100) battery  = 100;

    // charge_status keys off the charger STAT line (reliable), with charge rate as the
    // discharge/idle tiebreaker. power_source is inferred (no solar-sense line on V5).
    const char *charge_status = charging       ? "charging"
                              : (crate < -0.5f) ? "discharging"
                              : "idle";
    // power_source: USB if VBUS present; else Solar if the panel is measured producing
    // (SOLAR_SENSE) or the charger is charging on battery; else Battery.
    const char *power_source  = usb_present ? "USB"
                              : ((solar_present || charging) ? "Solar" : "Battery");

    char server_uri[256];
    snprintf(server_uri, sizeof(server_uri),
             "https://athome.rodlandfarms.com/api/esp/data?");  // TLS (root-CA bundle in POST())

    char httpRequestData[512];
    int n = snprintf(httpRequestData, sizeof(httpRequestData),
             "api_token=%s&hostname=%s&sensor=%s&location=%s&moisture=%d&batt=%.2f&battery_status=%d&charge_status=%s&power_source=%s",
             apiToken, hostname, sensorName, sensorLocation, moisture, battery, battery_status, charge_status, power_source);

    // V6: append temp/humidity/VPD only when an SHT40 was read (backend renames temp->temperature,
    // humid->humidity on ingest; absent => fields stay null, V5-identical on the wire).
    if (env_present && n > 0 && n < (int)sizeof(httpRequestData)) {
        snprintf(httpRequestData + n, sizeof(httpRequestData) - n,
                 "&temp=%.1f&humid=%.1f&vpd=%.2f", temp_f, humidity, vpd);
    }

    const int MAX_ATTEMPTS = 3;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        int httpResponseCode = POST(server_uri, httpRequestData);
        if (httpResponseCode == 200) {
            ESP_LOGI("UploadReadings", "POST ok (attempt %d/%d)", attempt, MAX_ATTEMPTS);
            return true;
        }
        ESP_LOGE("UploadReadings", "POST failed code=%d (attempt %d/%d)",
                 httpResponseCode, attempt, MAX_ATTEMPTS);
        if (attempt < MAX_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(1500 * attempt));  // linear backoff: 1.5 s, then 3 s
        }
    }
    ESP_LOGE("UploadReadings", "giving up after %d attempts — reading lost", MAX_ATTEMPTS);
    return false;
}

// Runs monitor() in its own task. monitor() does a TLS OTA check + uploads, which
// need a large stack; the WiFi event-handler task it used to run in is only 2304 B
// (CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE) and overflowed on the cert-bundle TLS
// handshake. monitor() normally ends in deep sleep and never returns.
void monitor_task(void *pvParameters){
    monitor();
    vTaskDelete(NULL);
}

// --- Power-source sensing (board V5 / Schematic.png: USB_DETECT=GPIO13, STAT=GPIO14) ---
#define USB_DETECT_GPIO  GPIO_NUM_13   // HIGH when USB (VBUS) present
#define STAT_GPIO        GPIO_NUM_14   // MCP73831 STAT: open-drain, LOW = charging

static void read_power_state(bool *usb_present, bool *charging) {
    static bool configured = false;
    if (!configured) {
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << USB_DETECT_GPIO),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,     // USB_DETECT is driven by the VBUS divider
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&io);
        io.pin_bit_mask = (1ULL << STAT_GPIO);
        io.pull_up_en = GPIO_PULLUP_ENABLE;        // STAT is open-drain -> needs a pull-up
        gpio_config(&io);
        configured = true;
    }
    *usb_present = gpio_get_level(USB_DETECT_GPIO) == 1;
    *charging    = gpio_get_level(STAT_GPIO) == 0;  // active-low
}

void monitor(){
    // Check for firmwareupdate
    check_update();
    //xTaskCreate(check_update, "check_update", 8192, NULL, 5, NULL);
    
    i2c_master_init();

#if ALRT_CONFIG_PCT > 0
    max17048_set_alert(ALRT_CONFIG_PCT);   // arm the low-battery ALRT (paired with ALRT_WAKE_GPIO)
#endif

    // Read battery and moisture data
    BatteryStatus battery = getBattery();
    int moisture = readMoisture();
    bool usb_present = false, charging = false;
    read_power_state(&usb_present, &charging);

    // V6: optional temperature/humidity (SHT40 @ 0x44). Absent on V5 -> env_present=false.
    float temp_f = 0.0f, humidity = 0.0f, vpd = 0.0f;
    bool env_present = false;
    if (sht40_present()) {
        float tc;
        if (sht40_read(&tc, &humidity)) {
            vpd = compute_vpd(tc, humidity);
            temp_f = tc * 9.0f / 5.0f + 32.0f;
            env_present = true;
            ESP_LOGI("SHT40", "T=%.1f°F  RH=%.0f%%  VPD=%.2f kPa", temp_f, humidity, vpd);
        }
    }

    // V6: optional measured solar (else false -> power_source falls back to STAT inference).
    bool solar_present = false; int solar_mv = 0;
    read_solar(&solar_present, &solar_mv);
    if (solar_present) ESP_LOGI("SOLAR", "panel ~%d mV (producing)", solar_mv);

    // Upload data — synchronous + retried; returns only after success or all attempts,
    // so no fixed post-upload delay is needed (the old vTaskDelay(3000) raced the 8 s
    // HTTP timeout and could sleep through an in-flight upload).
    bool uploaded = uploadReadings(moisture, battery.soc, battery.status, battery.crate,
                                   usb_present, charging, solar_present, temp_f, humidity,
                                   vpd, env_present, main_struct.hostname, main_struct.name,
                                   main_struct.location, main_struct.apiToken);
    ESP_LOGI("MONITOR", "upload %s", uploaded ? "succeeded" : "FAILED (reading lost)");

    // Sleep duration comes from NVS (set at provisioning via optional "sleep_seconds"
    // JSON key); defaults to 8 h. No more compile-time comment toggling.
    enter_deep_sleep(nvs_get_sleep_seconds());
}