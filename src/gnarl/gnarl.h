#ifndef _GNARL_H
#define _GNARL_H

#include <stdint.h>

#include "version.h"

#define TAG "GNARL"
#include <esp_log.h>

#define MILLISECONDS 1000
#define SECONDS 1000000

#define MHz 1000000

// Track github.com/ps2/rileylink/firmware/ble113_rfspy/gatt.xml
#define BLE_RFSPY_VERSION "ble_rfspy 2.0"

#define STATE_OK "OK"

typedef enum
{
    CONNECTION_STAT_BLE,
    CONNECTION_STAT_PUMP,
} connection_stat;

typedef struct
{
    uint32_t timestamp;
    int8_t rssi;
} connection_stats_t;

void gnarl_init(void);
void start_gnarl_task(void);
void rfspy_command(const uint8_t *buf, int count, int rssi);
void send_code(const uint8_t code);
void send_bytes(const uint8_t *buf, int count);
connection_stats_t* get_connection_stats(void);
void set_rssi(int value, connection_stat radio);

#define set_pump_rssi(value) set_rssi(value, CONNECTION_STAT_PUMP);
#define set_pump_disconnected() set_rssi(0, CONNECTION_STAT_PUMP);
#define set_ble_rssi(value) set_rssi(value, CONNECTION_STAT_BLE);
#define set_ble_disconnected() set_rssi(0, CONNECTION_STAT_BLE);

#endif // _GNARL_H
