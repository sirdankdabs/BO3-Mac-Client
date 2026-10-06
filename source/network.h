#ifndef BO3MACFIX_NETWORK_H
#define BO3MACFIX_NETWORK_H
#include <stdbool.h>
#include <stdint.h>

// T7Patch checksum transition: accept the previous password for 1500 ms.
void network_set_password(uint64_t password, uint64_t now_ms);
uint64_t network_get_password(void);
bool network_checksum_matches(uint16_t received, uint16_t plain, uint64_t now_ms);
bool network_has_prefix(uint64_t password);
bool network_instant_message_valid(const uint8_t *data, int length);
uint64_t network_now_ms(void);
#endif
