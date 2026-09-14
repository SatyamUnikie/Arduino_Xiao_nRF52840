#ifndef APP_COMMON_H_
#define APP_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#define APP_SESSION_KEY_LEN 16U

enum app_sensor_state {
	APP_SENSOR_STATE_INTACT = 0U,
	APP_SENSOR_STATE_BROKEN = 1U,
};

struct app_telemetry_plaintext {
	uint32_t sample_counter;
	uint16_t sensor_raw;
	uint8_t sensor_state;
	uint8_t battery_level;
	uint8_t flags;
	uint8_t reserved[3];
};

bool app_pairing_is_in_progress(void);

#endif /* APP_COMMON_H_ */
