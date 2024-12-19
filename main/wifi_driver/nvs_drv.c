#include <stdint.h>
#include <esp_err.h>
#include "esp_log.h"
#include "nvs_drv.h"
#include "nvs_flash.h"
#include "nvs.h"

esp_err_t save_to_nvs(const char *ssid, const char *password, char *name, char *location, uint8_t value)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    // Save SSID
    err = nvs_set_str(nvs_handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write SSID!");
        nvs_close(nvs_handle);
        return err;
    }

    // Save Password
    err = nvs_set_str(nvs_handle, "password", password);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write Password!");
        nvs_close(nvs_handle);
        return err;
    }

    // Save Name
    err = nvs_set_str(nvs_handle, "name", name);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write device name!");
        nvs_close(nvs_handle);
        return err;
    }

    // Save Location
    err = nvs_set_str(nvs_handle, "location", location);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write device location!");
        nvs_close(nvs_handle);
        return err;
    }

    // Save uint8_t value
    err = nvs_set_u8(nvs_handle, "wifi_value", value);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to write uint8_t!");
        nvs_close(nvs_handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to commit changes!");
    }

    // Close the handle
    nvs_close(nvs_handle);

    printf("NVS stored ssid %s\n", ssid);
    printf("NVS stored password %s\n", password);
    printf("NVS stored name %s\n", name);
    printf("NVS stored location %s\n", location);
    printf("NVS stored wifi_value %d\n", value);

    return err;
}

esp_err_t read_from_nvs(char *ssid, char *password, char *name, char *location, uint8_t *value)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t ssid_len = 32;
    size_t pass_len = 64;
    size_t name_len = 32;
    size_t location_len = 64;
    // Open NVS handle
    err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }

    // Read SSID

    err = nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "SSID not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "ssid", "");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default SSID!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read SSID!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Password
    
    err = nvs_get_str(nvs_handle, "password", password, &pass_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Password not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "password", "");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default password!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read Password!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Name
    
    err = nvs_get_str(nvs_handle, "name", name, &name_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Device name not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "name", "Device");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default device name!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read device name!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read Location
    
    err = nvs_get_str(nvs_handle, "location", location, &location_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "Device location not found. Setting default value.");
        err = nvs_set_str(nvs_handle, "location", "Location");
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default device location!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read device loction!");
        nvs_close(nvs_handle);
        return err;
    }

    // Read uint8_t value
    
    err = nvs_get_u8(nvs_handle, "wifi_value", value);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW("NVS", "wifi_value not found. Setting default value.");
        err = nvs_set_u8(nvs_handle, "wifi_value", 0);
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "Failed to set default uint8_t value!");
            nvs_close(nvs_handle);
            return err;
        }
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to read uint8_t!");
        nvs_close(nvs_handle);
        return err;
    }

    // Commit any changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Failed to commit changes!");
        nvs_close(nvs_handle);
        return err;
    }
    // Close the handle
    nvs_close(nvs_handle);

    return ESP_OK;
}
