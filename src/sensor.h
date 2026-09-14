#ifndef SENSOR_H_
#define SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

int breakwire_sensor_init(void);
int breakwire_sensor_read(uint16_t *raw_value, bool *is_broken);

#endif /* SENSOR_H_ */
