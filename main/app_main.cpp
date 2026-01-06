/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <bsp/esp-bsp.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <nvs_flash.h>
#include <cmath>
#include <app_openthread_config.h>
#include <app_reset.h>
#include <common_macros.h>
#include "led_strip.h"
// drivers implemented by this example
#include <drivers/veml7700_handler.h>

static const char *TAG = "app_main";

led_strip_handle_t led_strip;

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

led_strip_handle_t configure_led(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = 8,
        .max_leds = 1,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };

    led_strip_handle_t strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    return strip;
}

static esp_err_t factory_reset_button_register()
{
    button_handle_t push_button;
    esp_err_t err = bsp_iot_button_create(&push_button, NULL, BSP_BUTTON_NUM);
    VerifyOrReturnError(err == ESP_OK, err);
    return app_reset_button_register(push_button);
}

static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(commissionMgr.IsCommissioningWindowOpen() == false);

    // After removing last fabric, this example does not remove the Wi-Fi credentials
    // and still has IP connectivity so, only advertising on DNS-SD.
    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(300),
                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
    if (err != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed successfully");
        open_commissioning_window_if_necessary();
        break;

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        break;

    default:
        break;
    }
}

// This callback is invoked when clients interact with the Identify Cluster.
// In the callback implementation, an endpoint can identify itself. (e.g., by flashing an LED or light).
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

// This callback is called for every attribute update. The callback implementation shall
// handle the desired attributes and return an appropriate error code. If the attribute
// is not of your interest, please do not return an error code and strictly return ESP_OK.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == attribute::POST_UPDATE) {
        // On/Off logik (Cluster 0x0006)
        if (cluster_id == 0x0006 && attribute_id == 0x0000) {
            if (val->val.b) {
                led_strip_set_pixel(led_strip, 0, 50, 50, 50); 
            } else {
                led_strip_clear(led_strip);
            }
            led_strip_refresh(led_strip);
        }
        
        // Färg-logik (Color Control Cluster 0x0300)
        // När du ändrar i färghjulet kommer koden hit med cluster_id 0x0300
        if (cluster_id == 0x0300) {
            ESP_LOGI("LED", "Färgändring mottagen! ID: %ld", attribute_id);
            // Här kan vi senare lägga in HSV-till-RGB logik
        }
    }
    return ESP_OK;
}
// uint16_t lux_to_matter_value(uint32_t lux) {
//     if (lux <= 0) return 0; // Totalt mörkt
    
//     float measured_value = 10000 * log10((float)lux) + 1;
    
//     if (measured_value > 0xFFFE) return 0xFFFE; // Maxgräns
//     return (uint16_t)measured_value;
// }
extern "C" void app_main()
{
    /* Initialize the ESP NVS layer */
    nvs_flash_init();

    /* Initialize push button on the dev-kit to reset the device */
    esp_err_t err = factory_reset_button_register();
    ABORT_APP_ON_FAILURE(ESP_OK == err, ESP_LOGE(TAG, "Failed to initialize reset button, err:%d", err));

    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));


    endpoint::light_sensor::config_t sensor_config;
    sensor_config.illuminance_measurement.measured_value = 1; 
    endpoint_t *endpoint = endpoint::light_sensor::create(node, &sensor_config, ENDPOINT_FLAG_NONE, NULL);
    // Spara ID:t (oftast blir det 1 om det är första enheten)
    uint16_t sensor_endpoint_id = endpoint::get_id(endpoint);


    extended_color_light::config_t light_config;
    endpoint_t *endpoint_light = extended_color_light::create(node, &light_config, esp_matter::ENDPOINT_FLAG_NONE, NULL);
    uint16_t endpoint_id = endpoint::get_id(endpoint_light);
    uint32_t color_control_cluster_id = 0x0300;
    uint32_t feature_map_attribute_id = 0xFFFC;
    esp_matter_attr_val_t val = esp_matter_uint32(0x01); // 0x01 = Hue/Saturation
    attribute::update(endpoint_id, color_control_cluster_id, feature_map_attribute_id, &val);


    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    if (sensor_veml7700_init() == ESP_OK) {
        ESP_LOGI("MAIN", "Sensor redo!");
    }

    while (1) {
        uint32_t lux = 0;

        if (sensor_veml7700_read(&lux) == ESP_OK && lux > 0) {


            // Send to Home
            uint16_t matter_val =
                (uint16_t)(log10f((float)lux) * 10000.0f) + 1;

            esp_matter_attr_val_t attr =
                esp_matter_nullable_uint16(matter_val);

            esp_err_t err = attribute::update(
                sensor_endpoint_id,
                IlluminanceMeasurement::Id,
                IlluminanceMeasurement::Attributes::MeasuredValue::Id,
                &attr
            );

            if (err != ESP_OK) {
                ESP_LOGE("MAIN", "Matter update failed: %d", err);
            } else {
                ESP_LOGI("MAIN",
                        "Lux: %lu → Matter: %u",
                        lux, matter_val);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}