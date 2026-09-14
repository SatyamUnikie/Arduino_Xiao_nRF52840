#ifndef APP_EVENTS_H_
#define APP_EVENTS_H_

#include <zephyr/kernel.h>

enum app_event_type {
	APP_EVENT_NFC_TRIGGER = 0,
	APP_EVENT_BLE_CONNECTED,
	APP_EVENT_BLE_DISCONNECTED,
	APP_EVENT_BLE_PAIRED,
	APP_EVENT_SAMPLE_TICK,
	APP_EVENT_ADV_TIMEOUT,
};

struct app_event {
	enum app_event_type type;
};

int app_events_publish(const struct app_event *event, k_timeout_t timeout);
int app_events_receive(struct app_event *event, k_timeout_t timeout);

#endif /* APP_EVENTS_H_ */
