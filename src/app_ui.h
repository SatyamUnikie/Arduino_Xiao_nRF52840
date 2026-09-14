#ifndef APP_UI_H_
#define APP_UI_H_

#include <stdbool.h>

enum app_ui_cmd {
	APP_UI_CMD_SHOW_WAITING_NFC = 0,
	APP_UI_CMD_BLINK_NFC_SEEN,
	APP_UI_CMD_SHOW_PAIRING_IN_PROGRESS,
	APP_UI_CMD_SHOW_BLE_PAIRED,
	APP_UI_CMD_BLINK_DATA_PUSH,
	APP_UI_CMD_SET_LOW_BATTERY,
	APP_UI_CMD_FAULT,
};

int app_ui_init(void);
int app_ui_post(enum app_ui_cmd cmd, bool value);

#endif /* APP_UI_H_ */
