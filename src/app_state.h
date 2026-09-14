#ifndef APP_STATE_H_
#define APP_STATE_H_

#include <stdbool.h>

enum app_runtime_state {
	APP_STATE_BOOTING = 0,
	APP_STATE_WAITING_NFC,
	APP_STATE_PAIRING,
	APP_STATE_CONNECTED,
	APP_STATE_PAIRED,
	APP_STATE_FAULT,
};

void app_state_init(void);
void app_state_set_pairing(bool in_progress);
bool app_state_is_pairing(void);
void app_state_set_low_battery(bool active);
bool app_state_is_low_battery(void);
void app_state_set_connected(bool connected);
bool app_state_is_connected(void);
void app_state_set_paired(bool paired);
bool app_state_is_paired(void);
void app_state_set_fault(bool faulted);
bool app_state_is_faulted(void);
enum app_runtime_state app_state_get(void);

#endif /* APP_STATE_H_ */
