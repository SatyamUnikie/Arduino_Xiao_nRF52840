#ifndef LED_STATUS_H_
#define LED_STATUS_H_

#include <stdbool.h>

int app_led_status_init(void);
void app_led_show_waiting_nfc(void);
void app_led_blink_nfc_seen(void);
void app_led_show_pairing_in_progress(void);
void app_led_show_ble_paired(void);
void app_led_blink_data_push(void);
void app_led_set_low_battery(bool low_battery);
void app_led_fault(void);

#endif /* LED_STATUS_H_ */
