#ifndef SESSION_CRYPTO_H_
#define SESSION_CRYPTO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_common.h"

int app_session_key_generate(void);
const uint8_t *app_session_key_get(void);
bool app_session_key_is_ready(void);
void app_session_key_zeroize(void);
void app_session_xor_encrypt(uint8_t *buf, size_t len);

#endif /* SESSION_CRYPTO_H_ */
