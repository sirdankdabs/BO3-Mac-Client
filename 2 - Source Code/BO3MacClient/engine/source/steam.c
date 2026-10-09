#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "steam.h"
#include "network.h"
#include <pthread.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>

static CSteamAPIContext *ctx = NULL;

static CSteamID myId = { 0 };
static const char *originalName = NULL;

static ISteamFriends isteamfriends;
static ISteamApps isteamapps;

void init_steam_context(CSteamAPIContext *context) {
    ctx = context;
    // Do not cache our own replacements if Steam initializes this context again.
    static bool saved_originals = false;
    if (saved_originals)
        return;
    saved_originals = true;
    // keep note of our steamid and original name
    myId = (*ctx->m_pSteamUser)->GetSteamID(ctx->m_pSteamUser);
    originalName = (*ctx->m_pSteamFriends)->GetPersonaName(ctx->m_pSteamFriends);
    // keep track of our original vtables since we're overwriting the ones in the context
    memcpy(&isteamfriends, *ctx->m_pSteamFriends, sizeof(ISteamFriends));
    memcpy(&isteamapps, *ctx->m_pSteamApps, sizeof(ISteamApps));
}

CSteamID steam_id() { return myId; }
const char *steam_name() { return originalName; }

// according to steam support
#define MAX_FRIENDS_COUNT 2000
static CSteamID friendsCache[MAX_FRIENDS_COUNT];
static int numFriendsInCache;
static uint64_t friendsCheckedAt;
static bool friendsCached;
static pthread_mutex_t friends_lock = PTHREAD_MUTEX_INITIALIZER;

static void refresh_friends_locked(void) {
    numFriendsInCache = 0;
    if (!ctx || !isteamfriends.GetFriendCount || !isteamfriends.GetFriendByIndex) return;
    int count = isteamfriends.GetFriendCount(ctx->m_pSteamFriends, 0x04);
    for (int i = 0; i < count && i < MAX_FRIENDS_COUNT; ++i)
        friendsCache[numFriendsInCache++] = isteamfriends.GetFriendByIndex(ctx->m_pSteamFriends, i, 0x04);
    friendsCheckedAt = network_now_ms();
    friendsCached = true;
}
void steam_friends_refresh_cache(void) {
    pthread_mutex_lock(&friends_lock);
    refresh_friends_locked();
    pthread_mutex_unlock(&friends_lock);
}
bool steam_friends_with_user(uint64_t id) {
    pthread_mutex_lock(&friends_lock);
    uint64_t now = network_now_ms();
    if (!friendsCached || now < friendsCheckedAt || now - friendsCheckedAt >= 30000)
        refresh_friends_locked();
    bool found = false;
    for (int i = 0; i < numFriendsInCache; ++i)
        if (id == friendsCache[i].id) { found = true; break; }
    // Do not silently exclude friends whose lists exceed the bounded cache.
    if (!found && numFriendsInCache == MAX_FRIENDS_COUNT && isteamfriends.HasFriend)
        found = isteamfriends.HasFriend(ctx->m_pSteamFriends, (CSteamID){id}, 0x04);
    pthread_mutex_unlock(&friends_lock);
    return found;
}

// T7Patch's installed-content check, using Steam's Mac install directory.
static bool mode_content_present(AppId_t app) {
    const char *file;
    switch (app) {
        case 366840: file = "cp_common.xpak"; break;
        case 366841: file = "mp_common.xpak"; break;
        case 366842: file = "zm_common.xpak"; break;
        default: return true;
    }
    const char *disabled = getenv("BO3MACFIX_DISABLE_CONTENT_CHECK");
    if (disabled && !strcmp(disabled, "1")) return true;
    char root[PATH_MAX] = {0}, path[PATH_MAX];
    if (!isteamapps.GetAppInstallDir) return true;
    uint32_t length = isteamapps.GetAppInstallDir(ctx->m_pSteamApps, 311210, root, sizeof(root));
    if (!length || length >= sizeof(root) || !root[0]) return false;
    int n = snprintf(path, sizeof(path), "%s/zone/%s", root, file);
    struct stat info;
    return n > 0 && (size_t)n < sizeof(path) && !stat(path, &info) && S_ISREG(info.st_mode);
}

// there's like 26 dlc but what if they came back!!!1
#define MAX_DLC_COUNT 32
typedef struct _DlcCacheEntry {
    AppId_t app;
    bool installed;
    bool owned;
} DlcCacheEntry;
static DlcCacheEntry dlcCache[MAX_DLC_COUNT];
static int numDlcInCache;

// all released DLC for the game, so we don't need to wait for the game to request it
static const AppId_t allBo3DLCIDs[] = {
    830461, 830460, 830451, 830450, 660850, 648460, 624760, 581450,
    470190, 464370, 460110, 437351, 437350, 426730, 408630, 366850,
    366849, 366848, 366847, 366846, 366845, 366844, 366843, 366842,
    366841, 366840
};
static const int numBo3DLCIDs = 26;

// creates an entry in the cache table, or if it doesn't exist, creates it
// if we have too many entries then
static DlcCacheEntry *get_cache_entry(AppId_t appid) {
    // check if we have a cache entry for that appid
    for (int i = 0; i < numDlcInCache; i++) {
        if (dlcCache[i].app == appid)
            return &dlcCache[i];
    }
    // if we've reached the max amount then return null
    if (numDlcInCache == MAX_DLC_COUNT)
        return NULL;
    // otherwise create a new dlc cache entry
    DlcCacheEntry *new = &dlcCache[numDlcInCache++];
    new->app = appid;
    new->owned = isteamapps.BIsSubscribedApp(ctx->m_pSteamApps, appid);
    new->installed = isteamapps.BIsDlcInstalled(ctx->m_pSteamApps, appid) && mode_content_present(appid);
    printf("added %i to dlc cache (owned: %s, installed: %s)\n", new->app, new->owned ? "true" : "false", new->installed ? "true" : "false");
    return new;
}

void steam_dlc_refresh_cache() {
    numDlcInCache = 0;
    memset(dlcCache, 0, sizeof(dlcCache));
    for (int i = 0; i < numBo3DLCIDs; i++) {
        get_cache_entry(allBo3DLCIDs[i]);
    }
    printf("populated dlc cache with %i dlc\n", numDlcInCache);
}

bool steam_dlc_owns(AppId_t id) {
    DlcCacheEntry *cached = get_cache_entry(id);
    if (cached != NULL)
        return cached->owned;
    else
        return isteamapps.BIsSubscribedApp(ctx->m_pSteamApps, id);
}

bool steam_dlc_installed(AppId_t id) {
    DlcCacheEntry *cached = get_cache_entry(id);
    if (cached != NULL)
        return cached->installed;
    else
        return isteamapps.BIsDlcInstalled(ctx->m_pSteamApps, id) && mode_content_present(id);
}

// T7Patch avoids polling Steam download progress on every game frame.
// Preserve the Mac Steam API's bool return value as well as both output values.
typedef struct {
    AppId_t app;
    uint64_t downloaded, total, checked_at;
    bool valid;
} DownloadProgress;
static DownloadProgress progress_cache[MAX_DLC_COUNT];
static int progress_count;
static pthread_mutex_t progress_lock = PTHREAD_MUTEX_INITIALIZER;

bool steam_dlc_download_progress(AppId_t app, uint64_t *downloaded, uint64_t *total) {
    if (!ctx || !downloaded || !total)
        return false;
    uint64_t now = network_now_ms();
    pthread_mutex_lock(&progress_lock);
    DownloadProgress *entry = NULL;
    for (int i = 0; i < progress_count; ++i) {
        if (progress_cache[i].app == app) {
            entry = &progress_cache[i];
            break;
        }
    }
    if (entry && entry->valid && now >= entry->checked_at && now - entry->checked_at < 300000) {
        *downloaded = entry->downloaded;
        *total = entry->total;
        pthread_mutex_unlock(&progress_lock);
        return true;
    }
    bool result = isteamapps.GetDlcDownloadProgress(ctx->m_pSteamApps, app, downloaded, total);
    if (result) {
        if (!entry && progress_count < MAX_DLC_COUNT)
            entry = &progress_cache[progress_count++];
        if (entry)
            *entry = (DownloadProgress){app, *downloaded, *total, now, true};
    } else if (entry) {
        entry->valid = false;
    }
    pthread_mutex_unlock(&progress_lock);
    return result;
}
