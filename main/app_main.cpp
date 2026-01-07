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
#include <drivers/veml7700_handler.h>

static const char *TAG = "app_main";

static led_strip_handle_t led_strip;

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
    led_strip_handle_t handle;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &handle));
    return handle;
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

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static uint8_t s_hue = 0;
static uint8_t s_sat = 0;
static bool s_on = false;

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    if (type == attribute::POST_UPDATE) {
        if (cluster_id == 0x0006 && attribute_id == 0x0000) {
            s_on = val->val.b;
        }
        else if (cluster_id == 0x0300) {
            if (attribute_id == 0x0000) s_hue = val->val.u8;
            if (attribute_id == 0x0001) s_sat = val->val.u8;
        }

        if (s_on) {
            led_strip_set_pixel(led_strip, 0, (s_hue < 85 ? 50 : 0), (s_hue >= 85 && s_hue < 170 ? 50 : 0), (s_hue >= 170 ? 50 : 0));
        } else {
            led_strip_clear(led_strip);
        }
        led_strip_refresh(led_strip);
    }
    return ESP_OK;
}

extern "C" void app_main()
{
    nvs_flash_init();
    led_strip = configure_led(); 

    esp_err_t err = factory_reset_button_register();
    ABORT_APP_ON_FAILURE(ESP_OK == err, ESP_LOGE(TAG, "Failed to initialize reset button, err:%d", err));

    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    endpoint::light_sensor::config_t sensor_config;
    sensor_config.illuminance_measurement.measured_value = 1; 
    endpoint_t *endpoint = endpoint::light_sensor::create(node, &sensor_config, ENDPOINT_FLAG_NONE, NULL);
    uint16_t sensor_endpoint_id = endpoint::get_id(endpoint);

    extended_color_light::config_t light_config;
    endpoint_t *endpoint_light = extended_color_light::create(node, &light_config, esp_matter::ENDPOINT_FLAG_NONE, NULL);
    uint16_t endpoint_id = endpoint::get_id(endpoint_light);
    uint32_t color_control_cluster_id = 0x0300;
    uint32_t feature_map_attribute_id = 0xFFFC;
    esp_matter_attr_val_t val = esp_matter_uint32(0x01);
    attribute::update(endpoint_id, color_control_cluster_id, feature_map_attribute_id, &val);

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    if (sensor_veml7700_init() == ESP_OK) {
        ESP_LOGI("MAIN", "Sensor redo!");
    }

    while (1) {
        uint32_t lux = 0;

        if (sensor_veml7700_read(&lux) == ESP_OK) {
            bool should_be_on = (lux < 100);
            
            if (should_be_on) {
                led_strip_set_pixel(led_strip, 0, 50, 50, 50);
                ESP_LOGI("AUTO", "Mörkt ute (%lu lux) - Tänder lampan", lux);
            } else {
                led_strip_clear(led_strip);
                ESP_LOGI("AUTO", "Ljust ute (%lu lux) - Släcker lampan", lux);
            }
            led_strip_refresh(led_strip);

            esp_matter_attr_val_t on_off_val = esp_matter_bool(should_be_on);
            attribute::update(endpoint_id, 0x0006, 0x0000, &on_off_val);

            uint16_t matter_val = (uint16_t)(log10f((float)lux + 1.0f) * 10000.0f);
            esp_matter_attr_val_t attr = esp_matter_nullable_uint16(matter_val);
            attribute::update(sensor_endpoint_id, 0x0400, 0x0000, &attr);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}