#ifndef BO3MACFIX_STEAM_H
#define BO3MACFIX_STEAM_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "steam/CSteamAPIContext.h"
#include "steam/CCallback.h"

void init_steam_context(CSteamAPIContext *context);

CSteamID steam_id();
const char *steam_name();

void steam_friends_refresh_cache();
bool steam_friends_with_user(uint64_t id);

void steam_dlc_refresh_cache();
bool steam_dlc_owns(AppId_t id);
bool steam_dlc_installed(AppId_t id);

bool steam_dlc_download_progress(AppId_t app, uint64_t *downloaded, uint64_t *total);

void steam_install_extra_hooks(CSteamAPIContext *context);
#ifdef __cplusplus
}
#endif
#endif
