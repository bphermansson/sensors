#pragma once
#include <esp_err.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initierar I2C och VEML7700-sensorn
 * @return ESP_OK vid framgång
 */
esp_err_t sensor_veml7700_init();

/**
 * @brief Läser av ljusstyrka i Lux
 * @param lux Pekare där värdet lagras
 * @return ESP_OK vid framgång
 */
esp_err_t sensor_veml7700_read(uint32_t *lux);

#ifdef __cplusplus
}
#endif

