#include "led_status.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

enum app_led_base_state {
	APP_LED_BASE_WAITING_NFC = 0,
	APP_LED_BASE_BLE_PAIRED,
};

static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

static enum app_led_base_state base_state = APP_LED_BASE_WAITING_NFC;
static struct k_work_delayable restore_work;
static struct k_work_delayable pairing_work;
static struct k_work_delayable low_battery_work;
static bool pairing_active;
static bool pairing_led_on;
static bool low_battery_active;
static bool low_battery_led_on;
static bool initialized;

static int set_led(const struct gpio_dt_spec *led, bool on)
{
	return gpio_pin_set_dt(led, on ? 1 : 0);
}

static int show_rgb(bool red, bool green, bool blue)
{
	int err;

	err = set_led(&led_red, red);
	if (err != 0) {
		return err;
	}

	err = set_led(&led_green, green);
	if (err != 0) {
		return err;
	}

	return set_led(&led_blue, blue);
}

static int show_base_state(void)
{
	if (low_battery_active) {
		return show_rgb(low_battery_led_on, false, false);
	}

	if (pairing_active) {
		return show_rgb(false, false, true);
	}

	if (base_state == APP_LED_BASE_BLE_PAIRED) {
		return show_rgb(false, false, true);
	}

	return show_rgb(false, true, false);
}

static void pairing_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!initialized || !pairing_active) {
		return;
	}

	pairing_led_on = !pairing_led_on;
	(void)show_rgb(false, false, pairing_led_on);
	(void)k_work_reschedule(&pairing_work, K_MSEC(300));
}

static void restore_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (initialized) {
		(void)show_base_state();
	}
}

static void low_battery_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!initialized || !low_battery_active) {
		return;
	}

	low_battery_led_on = !low_battery_led_on;
	(void)show_rgb(low_battery_led_on, false, false);
	(void)k_work_reschedule(&low_battery_work, K_MSEC(300));
}

static void set_pairing_active(bool active)
{
	pairing_active = active;
	pairing_led_on = active;

	(void)k_work_cancel_delayable(&pairing_work);

	if (!initialized) {
		return;
	}

	if (active) {
		(void)show_rgb(false, false, true);
		(void)k_work_reschedule(&pairing_work, K_MSEC(300));
	} else {
		(void)show_base_state();
	}
}

void app_led_set_low_battery(bool low_battery)
{
	low_battery_active = low_battery;
	low_battery_led_on = low_battery;

	(void)k_work_cancel_delayable(&low_battery_work);

	if (!initialized) {
		return;
	}

	if (low_battery) {
		(void)show_rgb(true, false, false);
		(void)k_work_reschedule(&low_battery_work, K_MSEC(300));
	} else {
		(void)show_base_state();
	}
}

int app_led_status_init(void)
{
	int err;

	if (!device_is_ready(led_red.port) || !device_is_ready(led_green.port) ||
	    !device_is_ready(led_blue.port)) {
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	err = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	err = gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		return err;
	}

	k_work_init_delayable(&restore_work, restore_work_handler);
	k_work_init_delayable(&pairing_work, pairing_work_handler);
	k_work_init_delayable(&low_battery_work, low_battery_work_handler);
	initialized = true;

	return show_base_state();
}

void app_led_show_waiting_nfc(void)
{
	base_state = APP_LED_BASE_WAITING_NFC;
	set_pairing_active(false);

	if (initialized) {
		(void)k_work_cancel_delayable(&restore_work);
		(void)show_base_state();
	}
}

void app_led_blink_nfc_seen(void)
{
	if (!initialized) {
		return;
	}

	(void)show_rgb(false, true, false);
	(void)k_work_reschedule(&restore_work, K_MSEC(180));
}

void app_led_show_pairing_in_progress(void)
{
	if (!initialized) {
		return;
	}

	(void)k_work_cancel_delayable(&restore_work);
	set_pairing_active(true);
}

void app_led_show_ble_paired(void)
{
	base_state = APP_LED_BASE_BLE_PAIRED;
	set_pairing_active(false);

	if (initialized) {
		(void)k_work_cancel_delayable(&restore_work);
		(void)show_base_state();
	}
}

void app_led_blink_data_push(void)
{
	if (!initialized) {
		return;
	}

	(void)show_rgb(false, false, true);
	(void)k_work_reschedule(&restore_work, K_MSEC(180));
}

void app_led_fault(void)
{
	if (!initialized) {
		return;
	}

	(void)show_rgb(true, false, false);
	(void)k_work_reschedule(&restore_work, K_MSEC(400));
}
