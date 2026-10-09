#include "steam.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned calls;
static uint64_t now;
uint64_t network_now_ms(void) { return now; }
static bool succeed = true;
static uint64_t friend_id = 42;
static int friend_calls;
static char install_root[] = "/tmp/bo3-content.XXXXXX";
static int friend_count(void *ctx, int flags) { ++friend_calls; return 1; }
static CSteamID friend_at(void *ctx, int index, int flags) { return (CSteamID){friend_id}; }
static uint32_t install_dir(void *ctx, AppId_t app, char *out, uint32_t capacity) {
    assert(app == 311210); return (uint32_t)snprintf(out, capacity, "%s", install_root);
}
static bool content(void *ctx, AppId_t app) { return true; }

static CSteamID get_id(void *ctx) { return (CSteamID){.id = 123}; }
static const char *get_name(void *ctx) { return "test"; }
static bool progress(void *ctx, AppId_t app, uint64_t *downloaded, uint64_t *total) {
    ++calls;
    if (!succeed) return false;
    *downloaded = app;
    *total = app + 100;
    return true;
}
static bool replacement(void *ctx, AppId_t app, uint64_t *a, uint64_t *b) {
    assert(!"original Steam pointer was replaced by the hook");
    return false;
}
int main(void) {
    uint64_t a = 0, b = 0;
    assert(!steam_dlc_download_progress(1, &a, &b));
    ISteamUser user = {.GetSteamID = get_id}, *user_ptr = &user;
    ISteamFriends friends = {.GetPersonaName = get_name, .GetFriendCount = friend_count, .GetFriendByIndex = friend_at}, *friends_ptr = &friends;
    ISteamApps apps = {.GetDlcDownloadProgress = progress, .GetAppInstallDir = install_dir, .BIsSubscribedApp = content, .BIsDlcInstalled = content}, *apps_ptr = &apps;
    CSteamAPIContext ctx = {.m_pSteamUser = &user_ptr, .m_pSteamFriends = &friends_ptr, .m_pSteamApps = &apps_ptr};
    init_steam_context(&ctx);
    assert(steam_friends_with_user(42));
    friend_id = 99; now = 29999;
    assert(steam_friends_with_user(42) && friend_calls == 1);
    now = 30000;
    assert(steam_friends_with_user(99) && !steam_friends_with_user(42) && friend_calls == 2);
    now = 0;
    assert(mkdtemp(install_root));
    char zone[256], zm[256], mp[256];
    snprintf(zone,sizeof(zone),"%s/zone",install_root);assert(!mkdir(zone,0700));
    snprintf(zm,sizeof(zm),"%s/zm_common.xpak",zone);FILE *f=fopen(zm,"w");assert(f);fclose(f);
    snprintf(mp,sizeof(mp),"%s/mp_common.xpak",zone);assert(!mkdir(mp,0700));
    assert(!steam_dlc_installed(366840)); // Steam reports installed, file missing.
    assert(steam_dlc_owns(366840));      // Ownership is not altered.
    assert(steam_dlc_installed(366842)); // Regular mode content exists.
    assert(!steam_dlc_installed(366841)); // A directory is not a content file.
    assert(steam_dlc_installed(999));    // Other DLC is not hidden by this check.
    unlink(zm);rmdir(mp);rmdir(zone);rmdir(install_root);
    assert(!steam_dlc_download_progress(1, NULL, &b));
    assert(!steam_dlc_download_progress(1, &a, NULL));
    assert(calls == 0);
    assert(steam_dlc_download_progress(1, &a, &b));
    assert(a == 1 && b == 101 && calls == 1);
    a = b = 0;
    assert(steam_dlc_download_progress(1, &a, &b));
    assert(a == 1 && b == 101 && calls == 1);
    now = 299999;
    assert(steam_dlc_download_progress(1, &a, &b) && calls == 1);
    now = 300000;
    assert(steam_dlc_download_progress(1, &a, &b) && calls == 2);
    succeed = false;
    assert(!steam_dlc_download_progress(2, &a, &b));
    succeed = true;
    assert(steam_dlc_download_progress(2, &a, &b));
    assert(calls == 4 && a == 2 && b == 102);
    apps.GetDlcDownloadProgress = replacement;
    init_steam_context(&ctx);
    // Fill the bounded cache, then verify overflow falls back to Steam.
    for (AppId_t app = 3; app <= 40; ++app) {
        assert(steam_dlc_download_progress(app, &a, &b));
        assert(a == app && b == app + 100);
    }
    unsigned before = calls;
    assert(steam_dlc_download_progress(40, &a, &b));
    assert(calls == before + 1);
    puts("Steam cache regression tests passed.");
}
