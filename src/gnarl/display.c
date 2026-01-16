#include "gnarl.h"

#include <string.h>
#include <unistd.h>

#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include "adc.h"
#include "display.h"
#include "module.h"
#include "oled.h"

#define DISPLAY_TIMEOUT 5 // seconds
#define ROW_HEIGHT (OLED_HEIGHT / 4)
#define CHAR_WIDTH 8
#define CENTER_OFFSET(x, object_width) ((x - object_width) / 2)
#define Y_LINE_CENTER(y, object_height) (y * ROW_HEIGHT + CENTER_OFFSET(ROW_HEIGHT, object_height))

typedef struct
{
	uint8_t width;
	uint8_t height;
	const uint8_t *bits;
} glyph_t;

static TaskHandle_t task_handle;
static TimerHandle_t shutdown_timer;

static const glyph_t PHONE = {
	.height = 15,
	.width = 9,
	.bits = (uint8_t[]){0xfe, 0x00, 0x39, 0x01, 0x01, 0x01, 0x7d, 0x01, 0x45, 0x01, 0x45, 0x01, 0x45, 0x01, 0x45, 0x01, 0x45, 0x01, 0x45, 0x01, 0x7d, 0x01, 0x01, 0x01, 0x11, 0x01, 0x01, 0x01, 0xfe}};

static const glyph_t BATTERY = {
	.height = 9,
	.width = 15,
	.bits = (uint8_t[]){0xfc, 0x7f, 0x04, 0x7f, 0x07, 0x7f, 0x01, 0x7f, 0x01, 0x7f, 0x01, 0x7e, 0x07, 0x7e, 0x04, 0x7e, 0xfc, 0x7f}};

static const glyph_t PUMP = {
	.height = 9,
	.width = 15,
	.bits = (uint8_t[]){0xf0, 0x7f, 0x10, 0x40, 0xd1, 0x57, 0x51, 0x44, 0x51, 0x44, 0xd1, 0x57, 0x1d, 0x40, 0x06, 0x40, 0xfc, 0x7f}};

static const glyph_t CLOCK = {
	.height = 15,
	.width = 15,
	.bits = (uint8_t[]){0xe0, 0x03, 0x98, 0x0c, 0x84, 0x10, 0x02, 0x20, 0x82, 0x20, 0x81, 0x40, 0x81, 0x40, 0x87, 0x70, 0x01, 0x41, 0x01, 0x42, 0x02, 0x20, 0x02, 0x20, 0x84, 0x10, 0x98, 0x0c, 0xe0, 0x03}};

static void format_time_period(char *buf, uint32_t period)
{
	uint8_t seconds = period % 60;
	period /= 60;
	uint8_t minutes = period % 60;
	period /= 60;
	uint8_t hours = period % 24;
	period /= 24;
	uint8_t days = period;

	if (days > 0)
	{
		sprintf(buf, "%2dd%2dh", days, hours);
	}
	else if (hours > 0)
	{
		sprintf(buf, "%2dh%2dm", hours, minutes);
	}
	else if (minutes > 0)
	{
		sprintf(buf, "%2dm%2ds", minutes, seconds);
	}
	else
	{
		sprintf(buf, "%5ds", seconds);
	}
}

typedef struct
{
	int8_t rssi;
	uint32_t uptime;
} connection_display_data_t;

typedef struct
{
	connection_display_data_t connections[2];
	uint16_t battery_voltage;
	uint32_t uptime;
} display_data_t;

static inline uint32_t timestamp_to_period(uint32_t timestamp)
{
	return (esp_timer_get_time() / SECONDS) - timestamp;
}

static void draw_initial()
{
	oled_init();
	oled_font_medium();
	oled_font_monospace();
	oled_align_right();

	// draw phone glyph_t
	// 0-15
	oled_draw_xbm(CENTER_OFFSET(15, PHONE.width), Y_LINE_CENTER(0, PHONE.height), PHONE.width, PHONE.height, PHONE.bits);
	// draw pump glyph_t
	// 16-31
	oled_draw_xbm(CENTER_OFFSET(15, PUMP.width), Y_LINE_CENTER(1, PUMP.height), PUMP.width, PUMP.height, PUMP.bits);
	// draw battery glyph_t
	// 32-47
	oled_draw_xbm(CENTER_OFFSET(15, BATTERY.width), Y_LINE_CENTER(2, BATTERY.height), BATTERY.width, BATTERY.height, BATTERY.bits);
	// draw clock glyph_t
	// 48-63
	oled_draw_xbm(CENTER_OFFSET(15, CLOCK.width), Y_LINE_CENTER(3, CLOCK.height), CLOCK.width, CLOCK.height, CLOCK.bits);

	// draw labels
	oled_draw_string(OLED_WIDTH - 1 - CHAR_WIDTH * 7, Y_LINE_CENTER(0, 13), "dBm");
	oled_draw_string(OLED_WIDTH - 1 - CHAR_WIDTH * 7, Y_LINE_CENTER(1, 13), "dBm");
	oled_draw_string(OLED_WIDTH - 1, Y_LINE_CENTER(2, 13), "mV");
	oled_draw_string(OLED_WIDTH - 1 - 7 * CHAR_WIDTH, Y_LINE_CENTER(2, 13), "%");
}
static void draw_data()
{
	char buf[7];

	static display_data_t last_display_data = {
		.battery_voltage = UINT16_MAX,
		.uptime = UINT32_MAX,
		.connections = {
			{.uptime = UINT32_MAX, .rssi = INT8_MAX},
			{.uptime = UINT32_MAX, .rssi = INT8_MAX},
		},
	};
	uint8_t should_update = 0;

	// draw phone + pump RSSI
	for (int i = 0; i < 2; i++)
	{
		connection_display_data_t *last_connection_data = &last_display_data.connections[i];
		connection_stats_t *connection_data = &get_connection_stats()[i];

		const int8_t old_rssi = last_connection_data->rssi;
		const int8_t new_rssi = connection_data->rssi;

		if (old_rssi != new_rssi)
		{
			sprintf(buf, new_rssi ? "%4d" : " ---", new_rssi);
			oled_draw_string(OLED_WIDTH - 1 - 10 * CHAR_WIDTH, Y_LINE_CENTER(i, 13), buf);
			last_connection_data->rssi = new_rssi;
			should_update = 1;
		}

		const uint32_t old_period = last_display_data.uptime;
		const uint32_t new_period = timestamp_to_period(connection_data->timestamp);

		if (new_period != old_period)
		{
			format_time_period(buf, new_period);
			oled_draw_string(OLED_WIDTH - 1, Y_LINE_CENTER(i, 13), buf);
			last_connection_data->uptime = new_period;
			should_update = 1;
		}
	}

	// draw battery stats
	const uint16_t old_battery_voltage = last_display_data.battery_voltage;
	const uint16_t new_battery_voltage = get_battery_voltage();
	if (new_battery_voltage != old_battery_voltage)
	{
		sprintf(buf, "%4d", battery_percent(new_battery_voltage));
		oled_draw_string(OLED_WIDTH - 1 - 8 * CHAR_WIDTH, Y_LINE_CENTER(2, 13), buf);

		sprintf(buf, "%4d", new_battery_voltage);
		oled_draw_string(OLED_WIDTH - 1 - 2 * CHAR_WIDTH, Y_LINE_CENTER(2, 13), buf);
		last_display_data.battery_voltage = new_battery_voltage;
		should_update = 1;
	}

	// draw uptime
	const uint32_t old_uptime = last_display_data.uptime;
	const uint32_t new_uptime = timestamp_to_period(0);

	if (new_uptime != old_uptime)
	{
		format_time_period(buf, new_uptime);
		oled_draw_string(OLED_WIDTH - 1, Y_LINE_CENTER(3, 13), buf);
		last_display_data.uptime = new_uptime;
		should_update = 1;
	}

	if (should_update)
		oled_update();
}

static void display_loop(void *unused)
{
	draw_initial();

	ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

	const TickType_t REFRESH_TIMEOUT = pdMS_TO_TICKS(100);
	const TickType_t DISPLAY_DISABLED_TIMEOUT = pdMS_TO_TICKS(10000);
	TickType_t timeout = 0;

	// Notify the task itself to draw data immediately.
	xTaskNotify(task_handle, pdTRUE, eSetValueWithOverwrite);

	for (;;)
	{
		uint32_t notification_value;
		BaseType_t notified = xTaskNotifyWait(0, 0, &notification_value, timeout);

		esp_task_wdt_reset();

		if (notified == pdTRUE)
		{
			if (notification_value == pdFALSE)
			{
				timeout = DISPLAY_DISABLED_TIMEOUT;
				oled_off();
			}
			else
			{
				timeout = REFRESH_TIMEOUT;
				draw_data();
				oled_on();
			}
		}
		else if (timeout == REFRESH_TIMEOUT)
		{
			draw_data();
		}
	}
}

static inline void disable_display(TimerHandle_t xTimer)
{
	xTaskNotify(task_handle, pdFALSE, eSetValueWithOverwrite);
}

static void button_interrupt(void *unused)
{
	BaseType_t higher_priority_task_woken = pdFALSE;

	if (!gpio_get_level(BUTTON))
	{
		xTaskNotifyFromISR(task_handle, pdTRUE, eSetValueWithOverwrite, &higher_priority_task_woken);
		xTimerStopFromISR(shutdown_timer, &higher_priority_task_woken);
	}
	else
		xTimerResetFromISR(shutdown_timer, &higher_priority_task_woken);

	portYIELD_FROM_ISR(higher_priority_task_woken);
}

void display_init(void)
{
	xTaskCreate(display_loop, "display", 4096, 0, 10, &task_handle);
	shutdown_timer = xTimerCreate("display_off", pdMS_TO_TICKS(DISPLAY_TIMEOUT * 1000), pdFALSE, 0, disable_display);

	// Enable interrupt on button press.
	gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
	gpio_set_intr_type(BUTTON, GPIO_INTR_ANYEDGE);
	gpio_install_isr_service(0);
	gpio_isr_handler_add(BUTTON, button_interrupt, 0);
	gpio_intr_enable(BUTTON);

	xTimerStart(shutdown_timer, 0);
}
