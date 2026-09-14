#include "app_controller.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <hal/nrf_uicr.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "app_common.h"
#include "app_events.h"
#include "app_identity.h"
#include "app_state.h"
#include "app_ui.h"
#include "battery.h"
#include "ble.h"
#include "nfc_pairing.h"
#include "sensor.h"
#include "session_crypto.h"

LOG_MODULE_REGISTER(app_controller, LOG_LEVEL_INF);

static struct k_work_delayable sample_work;
static struct k_work_delayable adv_timeout_work;
static uint32_t sample_counter;

static void app_controller_ble_event_handler(enum app_ble_event event);
static void app_controller_nfc_trigger_handler(void);

static bool app_nfc_uicr_allows_nfc(void)
{
	uint32_t nfcpins = NRF_UICR->NFCPINS;
	bool nfc_enabled = ((nfcpins & UICR_NFCPINS_PROTECT_Msk) ==
			    (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos));

	LOG_INF("UICR.NFCPINS=0x%08x (%s)", nfcpins,
		nfc_enabled ? "NFC pins enabled" : "NFC pins disabled as GPIO");

	if (!nfc_enabled) {
		LOG_ERR("NFC will not work until UICR is erased/reset to NFC mode");
	}

	return nfc_enabled;
}

static void app_key_to_hex(const uint8_t *key, char *dst, size_t dst_len)
{
	size_t i;
	size_t idx = 0U;

	if ((key == NULL) || (dst == NULL) || (dst_len == 0U)) {
		return;
	}

	for (i = 0U; (i < APP_SESSION_KEY_LEN) && ((idx + 2U) < dst_len); ++i) {
		int written = snprintf(&dst[idx], dst_len - idx, "%02X", key[i]);

		if (written < 0) {
			break;
		}

		idx += (size_t)written;
	}

	dst[idx] = '\0';
}

static int app_prepare_session_key(void)
{
	int err;
	const uint8_t *key;
	char hex_key[(APP_SESSION_KEY_LEN * 2U) + 1U];

	app_session_key_zeroize();
	err = app_session_key_generate();
	if (err != 0) {
		return err;
	}

	key = app_session_key_get();
	if (key == NULL) {
		return -EINVAL;
	}

	memset(hex_key, 0, sizeof(hex_key));
	app_key_to_hex(key, hex_key, sizeof(hex_key));
	printk("SESSION_KEY_HEX:%s\n", hex_key);
	LOG_INF("Session key refreshed");

	return app_nfc_update_session_key(key);
}

static void app_pairing_begin(void)
{
	int err;

	err = app_ble_start_pairing_advertising();
	if (err != 0) {
		LOG_ERR("Failed to start BLE advertising from NFC trigger: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return;
	}

	app_state_set_pairing(true);
	(void)app_ui_post(APP_UI_CMD_SHOW_PAIRING_IN_PROGRESS, false);
	(void)k_work_reschedule(&adv_timeout_work,
				K_MSEC(CONFIG_APP_BLE_ADVERTISING_TIMEOUT_MS));
}

static void app_pairing_end(bool paired)
{
	(void)k_work_cancel_delayable(&adv_timeout_work);
	app_state_set_pairing(false);

	if (paired) {
		(void)app_ui_post(APP_UI_CMD_SHOW_BLE_PAIRED, false);
	} else {
		(void)app_ui_post(APP_UI_CMD_SHOW_WAITING_NFC, false);
	}
}

static void app_handle_sample_tick(void)
{
	int err;
	bool is_broken = false;
	uint16_t raw_value = 0U;
	uint8_t battery_level = 0U;
	struct app_telemetry_plaintext plaintext = {0};
	uint8_t encrypted_payload[sizeof(plaintext)];

	err = breakwire_sensor_read(&raw_value, &is_broken);
	if (err != 0) {
		LOG_ERR("Sensor read failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		goto reschedule;
	}

	err = app_battery_sample(&battery_level);
	if (err != 0) {
		LOG_ERR("Battery read failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		goto reschedule;
	}

	if (!app_state_is_low_battery()) {
		if (battery_level < CONFIG_APP_BATTERY_LOW_PERCENT) {
			app_state_set_low_battery(true);
		}
	} else if (battery_level >= CONFIG_APP_BATTERY_LOW_CLEAR_PERCENT) {
		app_state_set_low_battery(false);
	}
	(void)app_ui_post(APP_UI_CMD_SET_LOW_BATTERY, app_state_is_low_battery());

	if (app_ble_is_connected()) {
		(void)app_ble_set_battery_level(battery_level);
	}

	++sample_counter;
	plaintext.sample_counter = sample_counter;
	plaintext.sensor_raw = raw_value;
	plaintext.sensor_state = is_broken ? APP_SENSOR_STATE_BROKEN : APP_SENSOR_STATE_INTACT;
	plaintext.battery_level = battery_level;
	plaintext.flags = app_ble_is_paired() ? BIT(0) : 0U;

	if ((sample_counter % 10U) == 0U || is_broken) {
		LOG_INF("Sample=%u raw=%u state=%s batt=%u%% ble=%s paired=%s",
			sample_counter, raw_value, is_broken ? "BROKEN" : "INTACT",
			battery_level, app_ble_is_connected() ? "CONNECTED" : "DISCONNECTED",
			app_ble_is_paired() ? "YES" : "NO");
	}

	if (!app_ble_is_connected() || !app_session_key_is_ready()) {
		goto reschedule;
	}

	memcpy(encrypted_payload, &plaintext, sizeof(plaintext));
	app_session_xor_encrypt(encrypted_payload, sizeof(encrypted_payload));
	err = app_ble_notify_encrypted(encrypted_payload, sizeof(encrypted_payload));
	if (err != 0) {
		LOG_WRN("BLE notify deferred: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		goto reschedule;
	}

	(void)app_ui_post(APP_UI_CMD_BLINK_DATA_PUSH, false);

reschedule:
	(void)k_work_reschedule(&sample_work, K_MSEC(CONFIG_APP_SENSOR_SAMPLE_INTERVAL_MS));
}

static void app_controller_handle_event(const struct app_event *event)
{
	int err;

	switch (event->type) {
	case APP_EVENT_NFC_TRIGGER:
		if (app_state_is_connected() || app_state_is_pairing()) {
			break;
		}

		(void)app_ui_post(APP_UI_CMD_BLINK_NFC_SEEN, false);
		LOG_INF("NFC triggered BLE advertising start");
		app_pairing_begin();
		break;
	case APP_EVENT_BLE_CONNECTED:
		app_state_set_connected(true);
		(void)k_work_cancel_delayable(&adv_timeout_work);
		(void)app_ble_stop_advertising();
		break;
	case APP_EVENT_BLE_DISCONNECTED:
		app_state_set_connected(false);
		app_pairing_end(false);
		err = app_prepare_session_key();
		if (err != 0) {
			LOG_ERR("Session key refresh failed: %d", err);
			(void)app_ui_post(APP_UI_CMD_FAULT, false);
		}
		break;
	case APP_EVENT_BLE_PAIRED:
		app_state_set_paired(true);
		app_pairing_end(true);
		break;
	case APP_EVENT_SAMPLE_TICK:
		app_handle_sample_tick();
		break;
	case APP_EVENT_ADV_TIMEOUT:
		if (app_ble_is_advertising()) {
			LOG_INF("BLE advertising timeout expired");
			(void)app_ble_stop_advertising();
		}

		if (!app_ble_is_connected()) {
			app_pairing_end(false);
		}
		break;
	default:
		break;
	}
}

static void app_controller_thread(void *arg1, void *arg2, void *arg3)
{
	struct app_event event;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		(void)app_events_receive(&event, K_FOREVER);
		app_controller_handle_event(&event);
	}
}

K_THREAD_DEFINE(app_controller_tid, 2048, app_controller_thread, NULL, NULL, NULL, 6, 0, 0);

static void sample_work_handler(struct k_work *work)
{
	struct app_event event = {
		.type = APP_EVENT_SAMPLE_TICK,
	};

	ARG_UNUSED(work);
	(void)app_events_publish(&event, K_NO_WAIT);
}

static void adv_timeout_work_handler(struct k_work *work)
{
	struct app_event event = {
		.type = APP_EVENT_ADV_TIMEOUT,
	};

	ARG_UNUSED(work);
	(void)app_events_publish(&event, K_NO_WAIT);
}

static void app_controller_ble_event_handler(enum app_ble_event event)
{
	struct app_event app_event;

	switch (event) {
	case APP_BLE_EVENT_CONNECTED:
		app_event.type = APP_EVENT_BLE_CONNECTED;
		break;
	case APP_BLE_EVENT_DISCONNECTED:
		app_event.type = APP_EVENT_BLE_DISCONNECTED;
		break;
	case APP_BLE_EVENT_PAIRED:
		app_event.type = APP_EVENT_BLE_PAIRED;
		break;
	default:
		return;
	}

	(void)app_events_publish(&app_event, K_NO_WAIT);
}

static void app_controller_nfc_trigger_handler(void)
{
	struct app_event event = {
		.type = APP_EVENT_NFC_TRIGGER,
	};

	(void)app_events_publish(&event, K_NO_WAIT);
}

int app_controller_init(void)
{
	int err;

	app_state_init();

	err = app_ui_init();
	if (err != 0) {
		LOG_ERR("LED init failed: %d", err);
		return err;
	}

	(void)app_nfc_uicr_allows_nfc();

	err = app_nfc_pairing_init(app_controller_nfc_trigger_handler);
	if (err != 0) {
		LOG_ERR("NFC init failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	err = app_prepare_session_key();
	if (err != 0) {
		LOG_ERR("Session key setup failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	err = breakwire_sensor_init();
	if (err != 0) {
		LOG_ERR("Sensor init failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	err = app_battery_init();
	if (err != 0) {
		LOG_ERR("Battery init failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	err = app_ble_init(app_controller_ble_event_handler);
	if (err != 0) {
		LOG_ERR("BLE init failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	err = app_ble_set_device_name(app_identity_ble_name());
	if (err != 0) {
		LOG_ERR("BLE device name setup failed: %d", err);
		(void)app_ui_post(APP_UI_CMD_FAULT, false);
		return err;
	}

	(void)app_ui_post(APP_UI_CMD_SHOW_WAITING_NFC, false);
	LOG_INF("Wire Sensor started; waiting for NFC trigger");

	k_work_init_delayable(&adv_timeout_work, adv_timeout_work_handler);
	k_work_init_delayable(&sample_work, sample_work_handler);
	(void)k_work_reschedule(&sample_work, K_NO_WAIT);

	return 0;
}
