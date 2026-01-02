#include "veml7700_handler.h"
#include <veml7700.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "VEML_DRV";
static i2c_dev_t dev;
static veml7700_config_t config;

#define SDA_GPIO_NUM 5
#define SCL_GPIO_NUM 6

esp_err_t sensor_veml7700_init(void) {
    memset(&dev, 0, sizeof(i2c_dev_t));
    
    esp_err_t err = i2cdev_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    // Här lägger vi till (gpio_num_t) castingen:
    err = veml7700_init_desc(&dev, I2C_NUM_0, (gpio_num_t)SDA_GPIO_NUM, (gpio_num_t)SCL_GPIO_NUM);
    if (err != ESP_OK) return err;

    err = veml7700_probe(&dev);
    if (err != ESP_OK) return err;

    config.gain = VEML7700_GAIN_DIV_8;
    config.integration_time = VEML7700_INTEGRATION_TIME_100MS;
    config.power_saving_mode = VEML7700_POWER_SAVING_MODE_1000MS;
    config.power_saving_enable = true;
    config.shutdown = false;

    return veml7700_set_config(&dev, &config);
}
esp_err_t sensor_veml7700_read(uint32_t *lux) {
    if (!lux) return ESP_ERR_INVALID_ARG;
    return veml7700_get_ambient_light(&dev, &config, lux);
}