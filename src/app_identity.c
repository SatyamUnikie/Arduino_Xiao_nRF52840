#include "app_identity.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <hal/nrf_ficr.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app_identity, LOG_LEVEL_INF);

static char device_name[APP_DEVICE_NAME_LEN];
static bool initialized;

static void app_identity_generate_name(void)
{
	uint8_t eui64[8] = {0};
	int err;

	err = hwinfo_get_device_eui64(eui64);
	if (err != 0) {
		#if defined(NRF_FICR)
		uint32_t device_id_lo = NRF_FICR->DEVICEID[0];
		uint32_t device_id_hi = NRF_FICR->DEVICEID[1];
		uint64_t unique_id = ((uint64_t)device_id_hi << 32U) | (uint64_t)device_id_lo;

			LOG_WRN("hwinfo_get_device_eui64 failed: %d; using FICR DEVICEID", err);
			snprintf(device_name, sizeof(device_name), "%s%016llX",
				APP_DEVICE_NAME_PREFIX,
				(unsigned long long)unique_id);
			LOG_INF("Generated device BLE name: %s", device_name);
		#else
			LOG_ERR("hwinfo_get_device_eui64 failed: %d", err);
			memset(device_name, 0, sizeof(device_name));
			strncpy(device_name, "AS-UNKNOWN", sizeof(device_name) - 1U);
			device_name[sizeof(device_name) - 1U] = '\0';
			LOG_INF("Generated fallback device BLE name: %s", device_name);
		#endif
		device_name[sizeof(device_name) - 1U] = '\0';
		initialized = true;
		return;
	}

	snprintf(device_name, sizeof(device_name), "%s%02X%02X%02X%02X%02X%02X%02X%02X",
		 APP_DEVICE_NAME_PREFIX,
		eui64[0], eui64[1], eui64[2], eui64[3],
		eui64[4], eui64[5], eui64[6], eui64[7]);
	LOG_INF("Generated device BLE name: %s", device_name);

	device_name[sizeof(device_name) - 1U] = '\0';
	initialized = true;
}

int app_identity_get_ble_name(char *buf, size_t buf_len)
{
	if (!initialized) {
		app_identity_generate_name();
	}

	if ((buf == NULL) || (buf_len == 0U)) {
		return -EINVAL;
	}

	if (strlen(device_name) + 1U > buf_len) {
		return -ENOMEM;
	}

	memset(buf, 0, buf_len);
	strncpy(buf, device_name, buf_len - 1U);
	buf[buf_len - 1U] = '\0';
	return 0;
}

const char *app_identity_ble_name(void)
{
	if (!initialized) {
		app_identity_generate_name();
	}

	return device_name;
}
