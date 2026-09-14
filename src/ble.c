#include "ble.h"

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(app_ble, LOG_LEVEL_INF);

#define BT_UUID_WIRE_SERVICE_VAL BT_UUID_128_ENCODE(0x11223344, 0x5566, 0x7788, 0x99aa, 0xbbccddeeff01)
#define BT_UUID_WIRE_STATUS_VAL  BT_UUID_128_ENCODE(0x11223344, 0x5566, 0x7788, 0x99aa, 0xbbccddeeff02)

static struct bt_uuid_128 wire_service_uuid = BT_UUID_INIT_128(BT_UUID_WIRE_SERVICE_VAL);
static struct bt_uuid_128 wire_status_uuid = BT_UUID_INIT_128(BT_UUID_WIRE_STATUS_VAL);

static app_ble_event_handler_t event_handler;
static struct bt_conn *current_conn;
static bool advertising;
static bool paired;
static bool notify_enabled;
static char ble_device_name[32];
static char ble_adv_short_name[9];

static void app_ble_clear_peer_bond(struct bt_conn *conn, const char *why)
{
	const bt_addr_le_t *peer = bt_conn_get_dst(conn);
	int err;
	char addr[BT_ADDR_LE_STR_LEN];

	if (peer == NULL) {
		LOG_WRN("Cannot clear bond: peer address is unavailable (%s)", why);
		return;
	}

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	err = bt_unpair(BT_ID_DEFAULT, peer);
	if (err != 0) {
		LOG_WRN("bt_unpair failed for peer=%s (%s): %d", addr, why, err);
		return;
	}

	LOG_INF("Cleared stored bond for peer=%s (%s)", addr, why);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_WRN("BLE pairing cancelled by peer: conn=%p peer=%s", (void *)conn, addr);
}

static void auth_pairing_confirm(struct bt_conn *conn)
{
	int err;
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BLE pairing confirm requested: conn=%p peer=%s", (void *)conn, addr);

	err = bt_conn_auth_pairing_confirm(conn);
	if (err != 0) {
		LOG_ERR("bt_conn_auth_pairing_confirm failed: %d", err);
		return;
	}

	LOG_INF("BLE pairing accepted automatically for peer=%s", addr);
}

static void auth_pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("BLE pairing complete: conn=%p peer=%s bonded=%s", (void *)conn, addr,
		bonded ? "YES" : "NO");
}

static void auth_pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_ERR("BLE pairing failed: conn=%p peer=%s reason=%d", (void *)conn, addr, reason);
}

static void notify_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(wire_sensor_svc,
	BT_GATT_PRIMARY_SERVICE(&wire_service_uuid.uuid),
	BT_GATT_CHARACTERISTIC(&wire_status_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(notify_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_WIRE_SERVICE_VAL),
	BT_DATA(BT_DATA_NAME_SHORTENED, ble_adv_short_name, 0U),
};

static struct bt_data scan_rsp_data[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, ble_device_name, 0U),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("BLE connect callback: conn=%p err=%u", (void *)conn, err);

	if (err != 0U) {
		LOG_ERR("BLE connection failed: conn=%p err=%u", (void *)conn, err);
		return;
	}

	if (current_conn != NULL) {
		LOG_WRN("BLE connection callback ignored: current_conn already set (%p)", (void *)current_conn);
		return;
	}

	current_conn = bt_conn_ref(conn);
	advertising = false;

	LOG_INF("BLE connection accepted; requesting security level L2 (encrypted pairing)");
	int ret = bt_conn_set_security(conn, BT_SECURITY_L2);
	if (ret != 0) {
		LOG_ERR("bt_conn_set_security failed: %d", ret);
	}

	if (event_handler != NULL) {
		event_handler(APP_BLE_EVENT_CONNECTED);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("BLE disconnect callback: conn=%p reason=0x%02x", (void *)conn, reason);

	if (current_conn == conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	paired = false;
	notify_enabled = false;
	advertising = false;

	if (event_handler != NULL) {
		event_handler(APP_BLE_EVENT_DISCONNECTED);
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_INF("BLE security_changed: conn=%p level=%d err=%d", (void *)conn, level, err);

	if ((err == BT_SECURITY_ERR_SUCCESS) && (level >= BT_SECURITY_L1)) {
		paired = true;
		LOG_INF("BLE pairing/security established successfully (level=%d)", level);
		if (event_handler != NULL) {
			event_handler(APP_BLE_EVENT_PAIRED);
		}
	} else {
		LOG_ERR("BLE security negotiation not successful: level=%d err=%d", level, err);
		if ((err == BT_SECURITY_ERR_AUTH_REQUIREMENT) ||
		    (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING)) {
			app_ble_clear_peer_bond(conn, "security negotiation failed");
		}
	}
}

BT_CONN_CB_DEFINE(app_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static const struct bt_conn_auth_cb app_auth_callbacks = {
	.passkey_display = NULL,
	.passkey_entry = NULL,
	.cancel = auth_cancel,
	.pairing_confirm = auth_pairing_confirm,
};

static struct bt_conn_auth_info_cb app_auth_info_callbacks = {
	.pairing_complete = auth_pairing_complete,
	.pairing_failed = auth_pairing_failed,
};

int app_ble_init(app_ble_event_handler_t handler)
{
	int err;

	event_handler = handler;
	current_conn = NULL;
	advertising = false;
	paired = false;
	notify_enabled = false;
	memset(ble_device_name, 0, sizeof(ble_device_name));
	memset(ble_adv_short_name, 0, sizeof(ble_adv_short_name));
	strncpy(ble_device_name, CONFIG_BT_DEVICE_NAME, sizeof(ble_device_name) - 1U);
	strncpy(ble_adv_short_name, ble_device_name, sizeof(ble_adv_short_name) - 1U);
	adv_data[2].data_len = strlen(ble_adv_short_name);
	scan_rsp_data[0].data_len = strlen(ble_device_name);

	err = bt_enable(NULL);
	if (err != 0) {
		return err;
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		err = settings_load();
		if (err != 0) {
			return err;
		}
	}

	err = bt_conn_auth_cb_register(&app_auth_callbacks);
	if (err != 0) {
		LOG_ERR("bt_conn_auth_cb_register failed: %d", err);
		return err;
	}

	err = bt_conn_auth_info_cb_register(&app_auth_info_callbacks);
	if (err != 0) {
		LOG_ERR("bt_conn_auth_info_cb_register failed: %d", err);
		return err;
	}

	return 0;
}

int app_ble_set_device_name(const char *name)
{
	int err;

	if ((name == NULL) || (name[0] == '\0')) {
		return -EINVAL;
	}

	memset(ble_device_name, 0, sizeof(ble_device_name));
	memset(ble_adv_short_name, 0, sizeof(ble_adv_short_name));
	strncpy(ble_device_name, name, sizeof(ble_device_name) - 1U);
	strncpy(ble_adv_short_name, ble_device_name, sizeof(ble_adv_short_name) - 1U);
	ble_device_name[sizeof(ble_device_name) - 1U] = '\0';
	ble_adv_short_name[sizeof(ble_adv_short_name) - 1U] = '\0';

	err = bt_set_name(ble_device_name);
	if (err != 0) {
		LOG_ERR("bt_set_name failed: %d", err);
		return err;
	}

	adv_data[2].data_len = strlen(ble_adv_short_name);
	scan_rsp_data[0].data_len = strlen(ble_device_name);
	LOG_INF("BLE device name set: full=%s adv-short=%s", ble_device_name, ble_adv_short_name);
	return 0;
}

int app_ble_start_pairing_advertising(void)
{
	int err;

	LOG_INF("Starting BLE pairing advertising: current_conn=%p advertising=%d", (void *)current_conn, advertising);

	if (current_conn != NULL) {
		LOG_WRN("BLE advertising refused: active connection already exists");
		return -EALREADY;
	}

	if (advertising) {
		LOG_INF("BLE advertising already active");
		return 0;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data),
			      scan_rsp_data, ARRAY_SIZE(scan_rsp_data));
	if (err != 0) {
		LOG_ERR("BLE advertising start failed: %d", err);
		return err;
	}

	advertising = true;
	LOG_INF("BLE pairing advertising started; name=%s", ble_device_name);
	return 0;
}

int app_ble_stop_advertising(void)
{
	int err;

	if (!advertising) {
		return 0;
	}

	err = bt_le_adv_stop();
	if (err != 0) {
		LOG_ERR("BLE advertising stop failed: %d", err);
		return err;
	}

	advertising = false;
	LOG_INF("BLE advertising stopped");
	return 0;
}

int app_ble_notify_encrypted(const uint8_t *data, size_t len)
{
	if ((current_conn == NULL) || !notify_enabled || (data == NULL) || (len == 0U)) {
		return -EAGAIN;
	}

	return bt_gatt_notify(current_conn, &wire_sensor_svc.attrs[2], data, len);
}

bool app_ble_is_connected(void)
{
	return (current_conn != NULL);
}

bool app_ble_is_advertising(void)
{
	return advertising;
}

bool app_ble_is_paired(void)
{
	return paired;
}

int app_ble_set_battery_level(uint8_t level)
{
	return bt_bas_set_battery_level(level);
}
