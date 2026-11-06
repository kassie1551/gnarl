#include <stdio.h>

#include <esp_log.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <soc/rtc_io_reg.h>

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
	// Crystal current (method 2) comes from the touch pad in RTC_PERIPH; IDF keeps it on in sleep only for method 1.
	ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
	// Keep the 32 kHz crystal at its start-up drive; on this ESP32 revision it can stop in light sleep.
	REG_SET_FIELD(RTC_IO_XTAL_32K_PAD_REG, RTC_IO_DAC_XTAL_32K, 3);

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
