#include "app_ui.h"

#include <errno.h>

#include <zephyr/kernel.h>

#include "led_status.h"

struct app_ui_event {
	enum app_ui_cmd cmd;
	bool value;
};

K_MSGQ_DEFINE(app_ui_queue, sizeof(struct app_ui_event), 16, 4);

static void app_ui_thread(void *arg1, void *arg2, void *arg3)
{
	struct app_ui_event event;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)k_msgq_get(&app_ui_queue, &event, K_FOREVER);

		switch (event.cmd) {
		case APP_UI_CMD_SHOW_WAITING_NFC:
			app_led_show_waiting_nfc();
			break;
		case APP_UI_CMD_BLINK_NFC_SEEN:
			app_led_blink_nfc_seen();
			break;
		case APP_UI_CMD_SHOW_PAIRING_IN_PROGRESS:
			app_led_show_pairing_in_progress();
			break;
		case APP_UI_CMD_SHOW_BLE_PAIRED:
			app_led_show_ble_paired();
			break;
		case APP_UI_CMD_BLINK_DATA_PUSH:
			app_led_blink_data_push();
			break;
		case APP_UI_CMD_SET_LOW_BATTERY:
			app_led_set_low_battery(event.value);
			break;
		case APP_UI_CMD_FAULT:
			app_led_fault();
			break;
		default:
			break;
		}
	}
}

K_THREAD_DEFINE(app_ui_tid, 1024, app_ui_thread, NULL, NULL, NULL, 7, 0, 0);

int app_ui_init(void)
{
	return app_led_status_init();
}

int app_ui_post(enum app_ui_cmd cmd, bool value)
{
	struct app_ui_event event = {
		.cmd = cmd,
		.value = value,
	};

	return k_msgq_put(&app_ui_queue, &event, K_NO_WAIT);
}
