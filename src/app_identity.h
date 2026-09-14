#ifndef APP_IDENTITY_H_
#define APP_IDENTITY_H_

#include <stddef.h>

#define APP_DEVICE_NAME_PREFIX "AS-"
#define APP_DEVICE_NAME_LEN 32U

int app_identity_get_ble_name(char *buf, size_t buf_len);
const char *app_identity_ble_name(void);

#endif /* APP_IDENTITY_H_ */
