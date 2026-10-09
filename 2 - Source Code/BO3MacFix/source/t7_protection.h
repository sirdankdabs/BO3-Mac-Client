#ifndef MACFIX_T7_PROTECTION_H
#define MACFIX_T7_PROTECTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool t7_p2p_allowed(const uint8_t *data, size_t size, bool friends_only, bool friend_sender);
void t7_sanitize_chat(char *data, size_t size);
bool t7_model_path_valid(const char *path);
bool t7_sanitize_directives(const char *source, char output[4096]);
bool t7_sanitize_model_string(const char *source, char output[4096], bool *changed);
bool t7_connectionless_allowed(const char *command, bool campaign);
bool t7_menu_response_allowed(const char *response, bool local_host);
bool t7_lobby_value_valid(const char *key, int64_t value);
// Returns byte offset and replacement only for a fully matching campaign script.
bool t7_campaign_patch(const uint8_t *data, size_t size, size_t *offset,
                       const uint8_t **replacement, size_t *length);
#ifdef __cplusplus
}
#endif
#endif
