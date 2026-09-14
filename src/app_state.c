#include "app_state.h"

#include "app_common.h"

static enum app_runtime_state current_state = APP_STATE_BOOTING;
static bool pairing_in_progress;
static bool low_battery_active;
static bool ble_connected;
static bool ble_paired;
static bool faulted;

static void recompute_state(void)
{
	if (faulted) {
		current_state = APP_STATE_FAULT;
		return;
	}

	if (ble_paired) {
		current_state = APP_STATE_PAIRED;
		return;
	}

	if (ble_connected) {
		current_state = APP_STATE_CONNECTED;
		return;
	}

	if (pairing_in_progress) {
		current_state = APP_STATE_PAIRING;
		return;
	}

	current_state = APP_STATE_WAITING_NFC;
}

void app_state_init(void)
{
	current_state = APP_STATE_BOOTING;
	pairing_in_progress = false;
	low_battery_active = false;
	ble_connected = false;
	ble_paired = false;
	faulted = false;
	current_state = APP_STATE_WAITING_NFC;
}

void app_state_set_pairing(bool in_progress)
{
	pairing_in_progress = in_progress;
	recompute_state();
}

bool app_state_is_pairing(void)
{
	return pairing_in_progress;
}

void app_state_set_low_battery(bool active)
{
	low_battery_active = active;
}

bool app_state_is_low_battery(void)
{
	return low_battery_active;
}

void app_state_set_connected(bool connected)
{
	ble_connected = connected;
	if (!connected) {
		ble_paired = false;
	}
	recompute_state();
}

bool app_state_is_connected(void)
{
	return ble_connected;
}

void app_state_set_paired(bool paired)
{
	ble_paired = paired;
	if (paired) {
		ble_connected = true;
	}
	recompute_state();
}

bool app_state_is_paired(void)
{
	return ble_paired;
}

void app_state_set_fault(bool is_faulted)
{
	faulted = is_faulted;
	recompute_state();
}

bool app_state_is_faulted(void)
{
	return faulted;
}

enum app_runtime_state app_state_get(void)
{
	return current_state;
}

bool app_pairing_is_in_progress(void)
{
	return app_state_is_pairing();
}
