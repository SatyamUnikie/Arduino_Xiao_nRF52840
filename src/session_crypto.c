#include "session_crypto.h"

#include <zephyr/random/random.h>

static uint8_t session_key[APP_SESSION_KEY_LEN];
static bool key_ready;

static void secure_zero(uint8_t *buf, size_t len)
{
	volatile uint8_t *p = buf;

	while (len > 0U) {
		*p = 0U;
		++p;
		--len;
	}
}

int app_session_key_generate(void)
{
	sys_rand_get(session_key, sizeof(session_key));
	key_ready = true;
	return 0;
}

const uint8_t *app_session_key_get(void)
{
	return key_ready ? session_key : NULL;
}

bool app_session_key_is_ready(void)
{
	return key_ready;
}

void app_session_key_zeroize(void)
{
	secure_zero(session_key, sizeof(session_key));
	key_ready = false;
}

void app_session_xor_encrypt(uint8_t *buf, size_t len)
{
	size_t i;

	if ((buf == NULL) || !key_ready) {
		return;
	}

	for (i = 0U; i < len; ++i) {
		buf[i] ^= session_key[i % APP_SESSION_KEY_LEN];
	}
}
