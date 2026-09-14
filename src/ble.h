#ifndef BLE_H_
#define BLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum app_ble_event {
	APP_BLE_EVENT_CONNECTED,
	APP_BLE_EVENT_DISCONNECTED,
	APP_BLE_EVENT_PAIRED,
};

typedef void (*app_ble_event_handler_t)(enum app_ble_event event);

int app_ble_init(app_ble_event_handler_t handler);
int app_ble_set_device_name(const char *name);
int app_ble_start_pairing_advertising(void);
int app_ble_stop_advertising(void);
int app_ble_notify_encrypted(const uint8_t *data, size_t len);
bool app_ble_is_connected(void);
bool app_ble_is_advertising(void);
bool app_ble_is_paired(void);
int app_ble_set_battery_level(uint8_t level);

#endif /* BLE_H_ */
