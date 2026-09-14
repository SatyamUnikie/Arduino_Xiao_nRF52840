#ifndef BATTERY_H_
#define BATTERY_H_

#include <stdint.h>

int app_battery_init(void);
int app_battery_sample(uint8_t *percent);

#endif /* BATTERY_H_ */
