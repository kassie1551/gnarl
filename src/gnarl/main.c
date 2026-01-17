#include <stdio.h>

#include <esp_log.h>
#include <esp_pm.h>

#include "adc.h"
#include "display.h"
#include "gnarl.h"
#include "rfm95.h"
#include "spi.h"

#ifndef PUMP_FREQUENCY
#define PUMP_FREQUENCY (868500000)
#endif

/**
 * To enable power saving:
 * menuconfig -> Component config -> Power management -> Enable
 * menuconfig -> Component config -> FreeRTOS -> Kernel -> USE_TICKLESS_IDLE
 * menuconfig -> Component config -> Hardware Settings -> RTC clock source -> External 32MHz
 * menuconfig -> Component config -> Bluetooth -> Controller Options -> Modem Sleep -> Enable
 * menuconfig -> Component config -> Bluetooth -> Controller Options -> Modem Sleep -> Low Power Clock -> 32kHz
 */

void app_main(void)
{
	esp_pm_config_t pm_config = {
		.max_freq_mhz = 80,
		.min_freq_mhz = 20,
		.light_sleep_enable = true,
	};

	ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

	ESP_LOGI(TAG, "%s", SUBG_RFSPY_VERSION);

	rfm95_init();
	uint8_t v = read_version();
	ESP_LOGD(TAG, "radio version %d.%d", version_major(v), version_minor(v));
	set_frequency(PUMP_FREQUENCY);
	ESP_LOGD(TAG, "frequency set to %lu Hz", read_frequency());
	adc_init();
	display_init();
	gnarl_init();
}
