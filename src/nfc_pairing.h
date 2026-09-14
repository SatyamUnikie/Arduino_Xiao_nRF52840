#ifndef NFC_PAIRING_H_
#define NFC_PAIRING_H_

#include <stdint.h>

#include "app_common.h"

typedef void (*app_nfc_trigger_handler_t)(void);

int app_nfc_pairing_init(app_nfc_trigger_handler_t trigger_handler);
int app_nfc_update_session_key(const uint8_t key[APP_SESSION_KEY_LEN]);

#endif /* NFC_PAIRING_H_ */
