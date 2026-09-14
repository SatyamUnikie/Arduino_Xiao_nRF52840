#include "nfc_pairing.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <nfc_t2t_lib.h>

#include "app_common.h"
#include "app_identity.h"

LOG_MODULE_REGISTER(app_nfc_pairing, LOG_LEVEL_INF);

static app_nfc_trigger_handler_t trigger_cb;
static bool nfc_started;

#if defined(CONFIG_APP_ENABLE_NFC) && defined(CONFIG_NFC_T2T_NRFXLIB)
#include <nfc_t2t_lib.h>

#define NFC_TAG_MESSAGE_BUFFER 128U
#define NFC_TEXT_RECORD_LANG "en"

static uint8_t nfc_payload[NFC_TAG_MESSAGE_BUFFER];

static const char *nfc_event_to_str(nfc_t2t_event_t event)
{
	switch (event) {
	case NFC_T2T_EVENT_FIELD_ON:
		return "FIELD_ON";
	case NFC_T2T_EVENT_FIELD_OFF:
		return "FIELD_OFF";
	case NFC_T2T_EVENT_DATA_READ:
		return "DATA_READ";
	default:
		return "UNKNOWN";
	}
}

static void to_hex(const uint8_t *src, size_t src_len, char *dst, size_t dst_len)
{
	size_t i;
	size_t idx = 0U;

	for (i = 0U; (i < src_len) && (idx + 2U < dst_len); ++i) {
		int written = snprintf(&dst[idx], dst_len - idx, "%02X", src[i]);

		if (written < 0) {
			return;
		}
		idx += (size_t)written;
	}
}

static void nfc_callback(void *context, nfc_t2t_event_t event, const uint8_t *data, size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	LOG_INF("NFC event: %s", nfc_event_to_str(event));

	if (event == NFC_T2T_EVENT_FIELD_ON) {
		if (!app_pairing_is_in_progress()) {
			if (trigger_cb != NULL) {
				trigger_cb();
			}
		}
	}

	if (event == NFC_T2T_EVENT_DATA_READ) {
		LOG_INF("NFC tag read by reader");
	}
}

int app_nfc_pairing_init(app_nfc_trigger_handler_t trigger_handler)
{
	int err;

	trigger_cb = trigger_handler;
	err = nfc_t2t_setup(nfc_callback, NULL);
	if (err != 0) {
		LOG_ERR("nfc_t2t_setup failed: %d", err);
		return err;
	}

	err = nfc_t2t_payload_set(nfc_payload, sizeof(nfc_payload));
	if (err != 0) {
		LOG_ERR("nfc_t2t_payload_set failed: %d", err);
		return err;
	}

	err = nfc_t2t_emulation_start();
	if (err != 0) {
		LOG_ERR("nfc_t2t_emulation_start failed: %d", err);
		return err;
	}
	nfc_started = true;
	LOG_INF("NFC emulation started");

	return 0;
}

int app_nfc_update_session_key(const uint8_t key[APP_SESSION_KEY_LEN])
{
	char hex_key[(APP_SESSION_KEY_LEN * 2U) + 1U];
	char text_payload[(sizeof("name=") - 1U) + APP_DEVICE_NAME_LEN + (sizeof(";key=") - 1U) + (APP_SESSION_KEY_LEN * 2U) + 1U];
	const char *device_name;
	uint32_t encoded_len = sizeof(nfc_payload);
	int err;

	if (key == NULL) {
		return -EINVAL;
	}

	device_name = app_identity_ble_name();
	if (device_name == NULL) {
		return -EINVAL;
	}

	memset(hex_key, 0, sizeof(hex_key));
	to_hex(key, APP_SESSION_KEY_LEN, hex_key, sizeof(hex_key));

	memset(text_payload, 0, sizeof(text_payload));
	snprintf(text_payload, sizeof(text_payload), "name=%s;key=%s", device_name, hex_key);

	NFC_NDEF_MSG_DEF(nfc_msg, 1U);
	NFC_NDEF_TEXT_RECORD_DESC_DEF(text_record, UTF_8, NFC_TEXT_RECORD_LANG,
					     sizeof(NFC_TEXT_RECORD_LANG) - 1U,
					     (uint8_t *)text_payload,
					     (uint32_t)strlen(text_payload));

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_msg), &NFC_NDEF_TEXT_RECORD_DESC(text_record));
	if (err != 0) {
		LOG_ERR("nfc_ndef_msg_record_add failed: %d", err);
		return err;
	}

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_msg), nfc_payload, &encoded_len);
	if (err != 0) {
		LOG_ERR("nfc_ndef_msg_encode failed: %d", err);
		return err;
	}

	if (nfc_started) {
		err = nfc_t2t_emulation_stop();
		if (err != 0) {
			LOG_ERR("nfc_t2t_emulation_stop failed: %d", err);
			return err;
		}
		nfc_started = false;
		LOG_INF("NFC emulation stopped for payload refresh");
	}

	err = nfc_t2t_payload_set(nfc_payload, encoded_len);
	if (err != 0) {
		LOG_ERR("nfc_t2t_payload_set refresh failed: %d", err);
		return err;
	}

	err = nfc_t2t_emulation_start();
	if (err != 0) {
		LOG_ERR("nfc_t2t_emulation_start refresh failed: %d", err);
		return err;
	}
	nfc_started = true;
	LOG_INF("NFC payload refreshed with NDEF text record");

	return 0;
}

#else

int app_nfc_pairing_init(app_nfc_trigger_handler_t trigger_handler)
{
	trigger_cb = trigger_handler;

	if (IS_ENABLED(CONFIG_APP_ENABLE_NFC)) {
		LOG_WRN("NFC enabled in app, but CONFIG_NFC_T2T_NRFXLIB is unavailable");
	}

	return 0;
}

int app_nfc_update_session_key(const uint8_t key[APP_SESSION_KEY_LEN])
{
	ARG_UNUSED(key);
	return 0;
}

#endif
