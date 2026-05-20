#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hid_gap.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wisprkey_ble";

static const gpio_num_t BUTTON_PIN = GPIO_NUM_7;
static const TickType_t DEBOUNCE_TICKS = pdMS_TO_TICKS(25);

#define HID_RPT_ID_KEYBOARD 1
#define HID_RPT_ID_APPLE_FN 2
#define HID_KEY_F24 0x73

static esp_hidd_dev_t *s_hid_dev;
static bool s_connected;

static const uint8_t keyboard_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_RPT_ID_KEYBOARD,

    0x05, 0x07,        // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        // Usage Minimum (Left Control)
    0x29, 0xE7,        // Usage Maximum (Right GUI)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x08,        // Report Count (8)
    0x81, 0x02,        // Input (Data, Variable, Absolute)

    0x95, 0x01,        // Report Count (1)
    0x75, 0x08,        // Report Size (8)
    0x81, 0x03,        // Input (Constant)

    0x95, 0x05,        // Report Count (5)
    0x75, 0x01,        // Report Size (1)
    0x05, 0x08,        // Usage Page (LEDs)
    0x19, 0x01,        // Usage Minimum (Num Lock)
    0x29, 0x05,        // Usage Maximum (Kana)
    0x91, 0x02,        // Output (Data, Variable, Absolute)

    0x95, 0x01,        // Report Count (1)
    0x75, 0x03,        // Report Size (3)
    0x91, 0x03,        // Output (Constant)

    0x95, 0x06,        // Report Count (6)
    0x75, 0x08,        // Report Size (8)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, HID_KEY_F24, // Logical Maximum (F24)
    0x05, 0x07,        // Usage Page (Keyboard/Keypad)
    0x19, 0x00,        // Usage Minimum (Reserved)
    0x29, HID_KEY_F24, // Usage Maximum (F24)
    0x81, 0x00,        // Input (Data, Array, Absolute)

    0xC0,              // End Collection

    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined)
    0x09, 0x03,        // Usage (Apple Fn/Globe-style modifier)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_RPT_ID_APPLE_FN,
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x75, 0x01,        // Report Size (1)
    0x95, 0x01,        // Report Count (1)
    0x09, 0x03,        // Usage (Fn)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x75, 0x07,        // Report Size (7)
    0x95, 0x01,        // Report Count (1)
    0x81, 0x03,        // Input (Constant)
    0xC0,              // End Collection
};

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = keyboard_report_map,
        .len = sizeof(keyboard_report_map),
    },
};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id = 0x303A,
    .product_id = 0x4005,
    .version = 0x0100,
    .device_name = "WisprKey BLE",
    .manufacturer_name = "WisprKey",
    .serial_number = "0001",
    .report_maps = ble_report_maps,
    .report_maps_len = 1,
};

static void send_f24(bool pressed)
{
    if (!s_hid_dev || !s_connected || !esp_hidd_dev_connected(s_hid_dev)) {
        ESP_LOGW(TAG, "BLE not connected, skipped %s", pressed ? "press" : "release");
        return;
    }

    uint8_t apple_fn_report[1] = {pressed ? 0x01 : 0x00};
    esp_err_t err = esp_hidd_dev_input_set(s_hid_dev,
                                           0,
                                           HID_RPT_ID_APPLE_FN,
                                           apple_fn_report,
                                           sizeof(apple_fn_report));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send Apple Fn-style %s: %s",
                 pressed ? "press" : "release",
                 esp_err_to_name(err));
    }

    uint8_t report[8] = {0};
    if (pressed) {
        report[2] = HID_KEY_F24;
    }

    err = esp_hidd_dev_input_set(s_hid_dev, 0, HID_RPT_ID_KEYBOARD, report, sizeof(report));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send F24 %s: %s", pressed ? "press" : "release", esp_err_to_name(err));
    }
}

static void button_task(void *arg)
{
    (void)arg;

    bool stable_pressed = false;
    bool last_reading_pressed = false;
    TickType_t last_reading_change = xTaskGetTickCount();

    while (true) {
        const bool reading_pressed = gpio_get_level(BUTTON_PIN) == 0;

        if (reading_pressed != last_reading_pressed) {
            last_reading_pressed = reading_pressed;
            last_reading_change = xTaskGetTickCount();
        }

        if ((xTaskGetTickCount() - last_reading_change) >= DEBOUNCE_TICKS &&
            reading_pressed != stable_pressed) {
            stable_pressed = reading_pressed;
            ESP_LOGI(TAG, "%s F24", stable_pressed ? "Press" : "Release");
            send_f24(stable_pressed);
        }

        vTaskDelay(1);
    }
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "BLE HID started, advertising");
        ESP_ERROR_CHECK(esp_hid_ble_gap_adv_start());
        break;

    case ESP_HIDD_CONNECT_EVENT:
        if (param->connect.status == ESP_OK) {
            s_hid_dev = param->connect.dev;
            s_connected = true;
            ESP_LOGI(TAG, "BLE HID connected");
        } else {
            ESP_LOGE(TAG, "BLE HID connect failed: %s", esp_err_to_name(param->connect.status));
        }
        break;

    case ESP_HIDD_DISCONNECT_EVENT:
        s_connected = false;
        ESP_LOGI(TAG, "BLE HID disconnected, advertising again");
        ESP_ERROR_CHECK(esp_hid_ble_gap_adv_start());
        break;

    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGD(TAG, "Output report id=%u len=%u", param->output.report_id, param->output.length);
        break;

    default:
        break;
    }
}

void ble_hid_task_start_up(void)
{
    // esp_hid_gap.c shares Espressif's demo callback path; our real task starts in app_main.
}

static void init_button(void)
{
    const gpio_config_t button_config = {
        .pin_bit_mask = BIT64(BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    init_button();

    ESP_LOGI(TAG, "Starting BLE HID keyboard");
    ESP_ERROR_CHECK(esp_hid_gap_init(HIDD_BLE_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, ble_hid_config.device_name));
    ESP_ERROR_CHECK(esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&ble_hid_config,
                                      ESP_HID_TRANSPORT_BLE,
                                      ble_hidd_event_callback,
                                      &s_hid_dev));

    xTaskCreate(button_task, "button_task", 3072, NULL, configMAX_PRIORITIES - 3, NULL);
}
