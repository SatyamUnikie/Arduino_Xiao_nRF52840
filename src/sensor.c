#include "sensor.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define BREAKWIRE_USER_NODE DT_PATH(zephyr_user)

static const struct gpio_dt_spec breakwire_power =
	GPIO_DT_SPEC_GET(BREAKWIRE_USER_NODE, breakwire_power_gpios);
static const struct adc_dt_spec breakwire_adc =
	ADC_DT_SPEC_GET_BY_IDX(BREAKWIRE_USER_NODE, 0);

static bool initialized;

static int sample_breakwire_adc(uint32_t *average_value)
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
		err = adc_sequence_init_dt(&breakwire_adc, &sequence);
		if (err != 0) {
			return err;
		}

		err = adc_read_dt(&breakwire_adc, &sequence);
		if (err != 0) {
			return err;
		}

		if (sample_buffer < 0) {
			sample_buffer = 0;
		}

		sum += (uint32_t)sample_buffer;
		k_sleep(K_MSEC(2));
	}

	*average_value = sum / CONFIG_APP_ADC_SAMPLE_COUNT;
	return 0;
}

int breakwire_sensor_init(void)
{
	int err;

	if (!device_is_ready(breakwire_power.port)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&breakwire_power, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	if (!adc_is_ready_dt(&breakwire_adc)) {
		return -ENODEV;
	}

	err = adc_channel_setup_dt(&breakwire_adc);
	if (err != 0) {
		return err;
	}

	initialized = true;
	return 0;
}

int breakwire_sensor_read(uint16_t *raw_value, bool *is_broken)
{
	int err;
	uint32_t average_value = 0U;

	if (!initialized || (raw_value == NULL) || (is_broken == NULL)) {
		return -EINVAL;
	}

	err = gpio_pin_set_dt(&breakwire_power, 1);
	if (err != 0) {
		return err;
	}

	k_sleep(K_MSEC(5));

	err = sample_breakwire_adc(&average_value);

	(void)gpio_pin_set_dt(&breakwire_power, 0);

	if (err != 0) {
		return err;
	}

	*raw_value = (uint16_t)average_value;
	*is_broken = (average_value < (uint32_t)CONFIG_APP_SENSOR_BROKEN_THRESHOLD);

	return 0;
}
