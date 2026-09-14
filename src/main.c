#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_controller.h"

LOG_MODULE_REGISTER(wire_sensor_main, LOG_LEVEL_INF);

int main(void)
{
	int err;

	err = app_controller_init();
	if (err != 0) {
		LOG_ERR("App controller init failed: %d", err);
		return err;
	}

	while (true) {
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
