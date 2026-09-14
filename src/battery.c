#include "battery.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define BATTERY_USER_NODE DT_PATH(zephyr_user)
#define BATTERY_READ_SETTLE_MS 5

struct lipo_ocv_point {
	int32_t mv;
	uint8_t percent;
};

/*
 * LiPo open-circuit voltage profile for 1S cells.
 * Percent is linearly interpolated between adjacent points.
 */
static const struct lipo_ocv_point lipo_ocv_profile[] = {
	{ 3300, 0 },
	{ 3400, 1 },
	{ 3500, 4 },
	{ 3600, 9 },
	{ 3650, 14 },
	{ 3700, 24 },
	{ 3750, 36 },
	{ 3800, 46 },
	{ 3850, 55 },
	{ 3900, 64 },
	{ 3950, 72 },
	{ 4000, 78 },
	{ 4050, 84 },
	{ 4100, 90 },
	{ 4150, 95 },
	{ 4200, 100 },
};

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET_BY_IDX(BATTERY_USER_NODE, 1);
static const struct gpio_dt_spec battery_read_enable =
	GPIO_DT_SPEC_GET(BATTERY_USER_NODE, battery_read_enable_gpios);

#if CONFIG_APP_BATTERY_USB_AWARE_ENABLE_GATING
#if DT_NODE_HAS_PROP(BATTERY_USER_NODE, battery_usb_present_gpios)
static const struct gpio_dt_spec battery_usb_present =
	GPIO_DT_SPEC_GET(BATTERY_USER_NODE, battery_usb_present_gpios);
#define APP_BATTERY_HAS_USB_PRESENT_GPIO 1
#else
#define APP_BATTERY_HAS_USB_PRESENT_GPIO 0
#endif
#endif

static int set_battery_read_path_enabled(bool enabled)
{
	return gpio_pin_set_dt(&battery_read_enable, enabled ? 1 : 0);
}

#if CONFIG_APP_BATTERY_USB_AWARE_ENABLE_GATING
static int usb_power_present(bool *present)
{
	int value;

	if (present == NULL) {
		return -EINVAL;
	}

#if !APP_BATTERY_HAS_USB_PRESENT_GPIO
	return -ENOTSUP;
#else
	value = gpio_pin_get_dt(&battery_usb_present);
	if (value < 0) {
		return value;
	}

	*present = (value != 0);
	return 0;
#endif
}
#endif

int app_battery_init(void)
{
	int err;
#if CONFIG_APP_BATTERY_USB_AWARE_ENABLE_GATING
	bool usb_present = false;
#endif

	if (!device_is_ready(battery_read_enable.port)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&battery_read_enable, GPIO_OUTPUT_ACTIVE);
	if (err != 0) {
		return err;
	}

	if (!adc_is_ready_dt(&battery_adc)) {
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&battery_adc);
	if (err != 0) {
		return err;
	}

#if CONFIG_APP_BATTERY_USB_AWARE_ENABLE_GATING
#if !APP_BATTERY_HAS_USB_PRESENT_GPIO
	return -ENOTSUP;
#else
	if (!device_is_ready(battery_usb_present.port)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&battery_usb_present, GPIO_INPUT);
	if (err != 0) {
		return err;
	}

	err = usb_power_present(&usb_present);
	if (err != 0) {
		return err;
	}

	if (!usb_present) {
		err = set_battery_read_path_enabled(false);
		if (err != 0) {
			return err;
		}
	}
#endif
#endif

	return 0;
}

static int sample_battery_adc(uint32_t *average_mv)
{
	int err;
	uint32_t sum = 0U;
	uint8_t i;
	int16_t sample_buffer = 0;
	struct adc_sequence sequence = {
		.buffer = &sample_buffer,
		.buffer_size = sizeof(sample_buffer),
	};

	for (i = 0U; i < CONFIG_APP_ADC_SAMPLE_COUNT; ++i) {
		err = adc_sequence_init_dt(&battery_adc, &sequence);
		if (err != 0) {
			return err;
		}

		err = adc_read_dt(&battery_adc, &sequence);
		if (err != 0) {
			return err;
		}

		if (sample_buffer < 0) {
			sample_buffer = 0;
		}

		sum += (uint32_t)sample_buffer;
		k_sleep(K_MSEC(2));
	}

	*average_mv = sum / CONFIG_APP_ADC_SAMPLE_COUNT;
	return 0;
}

static uint8_t lipo_percent_from_mv(int32_t mv)
{
	size_t i;

	if (mv <= lipo_ocv_profile[0].mv) {
		return 0U;
	}

	if (mv >= lipo_ocv_profile[ARRAY_SIZE(lipo_ocv_profile) - 1U].mv) {
		return 100U;
	}

	for (i = 1U; i < ARRAY_SIZE(lipo_ocv_profile); ++i) {
		const struct lipo_ocv_point *lo = &lipo_ocv_profile[i - 1U];
		const struct lipo_ocv_point *hi = &lipo_ocv_profile[i];
		int32_t span_mv;
		int32_t delta_mv;
		int32_t pct_span;
		int32_t interp;

		if (mv > hi->mv) {
			continue;
		}

		span_mv = hi->mv - lo->mv;
		delta_mv = mv - lo->mv;
		pct_span = (int32_t)hi->percent - (int32_t)lo->percent;

		interp = (int32_t)lo->percent + (delta_mv * pct_span) / span_mv;
		if (interp < 0) {
			return 0U;
		}
		if (interp > 100) {
			return 100U;
		}

		return (uint8_t)interp;
	}

	return 0U;
}

int app_battery_sample(uint8_t *percent)
{
	int err;
	int32_t mv;
	uint32_t average_mv = 0U;
	bool keep_read_path_enabled = true;

	if (percent == NULL) {
		return -EINVAL;
	}

#if CONFIG_APP_BATTERY_USB_AWARE_ENABLE_GATING
	err = usb_power_present(&keep_read_path_enabled);
	if (err != 0) {
		return err;
	}
#endif

	err = set_battery_read_path_enabled(true);
	if (err != 0) {
		return err;
	}

	k_sleep(K_MSEC(BATTERY_READ_SETTLE_MS));

	err = sample_battery_adc(&average_mv);
	if (err != 0) {
		if (!keep_read_path_enabled) {
			(void)set_battery_read_path_enabled(false);
		}
		return err;
	}

	if (!keep_read_path_enabled) {
		err = set_battery_read_path_enabled(false);
		if (err != 0) {
			return err;
		}
	}

	mv = (int32_t)average_mv;
	err = adc_raw_to_millivolts_dt(&battery_adc, &mv);
	if (err != 0) {
		return err;
	}

	mv = (int32_t)(((int64_t)mv * CONFIG_APP_BATTERY_DIVIDER_NUMERATOR) /
		       CONFIG_APP_BATTERY_DIVIDER_DENOMINATOR);

	if (mv <= CONFIG_APP_BATTERY_EMPTY_MV) {
		*percent = 0U;
		return 0;
	}

	if (mv >= CONFIG_APP_BATTERY_FULL_MV) {
		*percent = 100U;
		return 0;
	}

	*percent = lipo_percent_from_mv(mv);
	return 0;
}
