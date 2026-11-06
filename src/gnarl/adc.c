#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "module.h"

#include "adc.h"

const static char *TAG = "ADC";

#define AVG_SAMPLES (10)
#define AVG_PERIOD (4) // seconds
#define SCALE_VOLTAGE(raw_mv) (raw_mv * (100 + 220) / 100)
#define LOW_BATTERY_READINGS (3) // consecutive, so a dip while transmitting doesn't sleep

static int battery_voltage;

uint16_t get_battery_voltage(void)
{
    return battery_voltage;
}

uint8_t battery_percent(uint16_t battery_voltage)
{
    uint16_t full_bat = 4200;
    uint16_t empty_bat = 3000;

    if (battery_voltage > full_bat)
        battery_voltage = full_bat;
    else if (battery_voltage < empty_bat)
        battery_voltage = empty_bat;

    uint8_t percent = (battery_voltage - empty_bat) * 100 / (full_bat - empty_bat);

    ESP_LOGI(TAG, "(Conv) Battery voltage: %d mV, percentage: %d%%", battery_voltage, percent);

    return percent;
}

static void adc_task(void *arg)
{
    gpio_set_direction(GPIO_NUM_21, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_21, 0);

    adc_unit_t unit;
    adc_channel_t channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(BATTERY_ADC, &unit, &channel));

    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &config));

    adc_cali_handle_t cali_handle;

    adc_cali_line_fitting_config_t cali_config = {
        .atten = config.atten,
        .bitwidth = config.bitwidth,
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &cali_handle));

    TickType_t last_wake_time = xTaskGetTickCount();
    int low_readings = 0;

    for (;;)
    {
        int averaged_result = 0;
        for (int i = 0; i < AVG_SAMPLES; i++)
        {
            int raw_output, voltage;
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, channel, &raw_output));
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw_output, &voltage));
            ESP_LOGI(TAG, "ADC raw: %d, voltage: %d mV", raw_output, voltage);

            if (battery_voltage <= 1000)
                battery_voltage = SCALE_VOLTAGE(voltage);

            averaged_result += voltage;

            vTaskDelay(pdMS_TO_TICKS(AVG_PERIOD * 1000 / AVG_SAMPLES));
        }
        averaged_result /= AVG_SAMPLES;

        ESP_LOGI(TAG, "ADC voltage: %d mV (%d mV scaled)", averaged_result, SCALE_VOLTAGE(averaged_result));
        battery_voltage = SCALE_VOLTAGE(averaged_result);

        low_readings = battery_voltage < 3000 ? low_readings + 1 : 0;
        if (low_readings >= LOW_BATTERY_READINGS)
        {
            esp_sleep_enable_ext0_wakeup(BUTTON, 0);
            esp_deep_sleep_start();
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(60000));
    }
}

void adc_init(void)
{
    xTaskCreate(adc_task, "adc", 3072, NULL, 5, NULL);
}