#ifndef MACFIX_T7_RUNTIME_H
#define MACFIX_T7_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void t7_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
void *t7_resolve_function(const char *name);
// True when the running executable is the Mac build t7_mac_symbols.h describes.
bool t7_known_mac_build(void);
bool t7_memory_readable(const void *address, size_t size);
bool t7_memory_writable(const void *address, size_t size);
void t7_install_hooks(void);
void t7_init_lobby_inspector(void);
bool t7_inspect_lobby(void *message);
void t7_install_lua_symbols(void);
bool get_friends_only(void);
void set_friends_only(bool enabled);
bool bo3_is_campaign(void);
bool bo3_is_zombies(void);
// BOIII/Ezz community servers (boiii_probe.c, boiii_client.c)
void boiii_register(void);
void boiii_client_register(void);
void boiii_client_install(void);
void boiii_frame(void);
bool boiii_connectionless(const char *command, const void *msg);
bool boiii_session_active(void);
#ifdef __cplusplus
}
#endif
#endif
