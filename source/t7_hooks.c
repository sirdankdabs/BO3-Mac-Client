#include "t7_runtime.h"
#include "t7_protection.h"
#include "steam.h"
#include "dobby.h"
#include "versioninfo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

// Only functions resolved from the Mac executable are installed. These native
// signatures use the SysV ABI; no Win32 calling conventions or RVAs are used.
#define HOOK(name, result, ...) static result (*orig_##name)(__VA_ARGS__); static result hook_##name(__VA_ARGS__)
HOOK(LobbyMsgRW_PackageInt, bool, void *msg, const char *key, int32_t *value) {
    return value && key && orig_LobbyMsgRW_PackageInt(msg, key, value) && t7_lobby_value_valid(key, *value);
}
HOOK(LobbyMsgRW_PackageUInt, bool, void *msg, const char *key, uint32_t *value) {
    // Datamask handling needs a verified session-state interpretation and is
    // intentionally not borrowed from the Windows global layout.
    return value && key && orig_LobbyMsgRW_PackageUInt(msg, key, value) && t7_lobby_value_valid(key, *value);
}
HOOK(LobbyMsgRW_PackageUChar, bool, void *msg, const char *key, uint8_t *value) {
    if (!value || !key || !orig_LobbyMsgRW_PackageUChar(msg, key, value)) return false;
    if (!strcasecmp(key, "nattype") && *value > 4) *value = 4;
    return true;
}
HOOK(SEH_ReplaceDirectiveInStringWithBinding, const char *, int client, const char *source, char *dest) {
    char input[4096];
    if (!dest || !t7_sanitize_directives(source, input)) return NULL;
    return orig_SEH_ReplaceDirectiveInStringWithBinding(client, input, dest);
}
HOOK(UI_DoModelStringReplacement, bool, int client, char *element, const char *source, char *dest, unsigned capacity) {
    char input[4096]; bool changed;
    if (!dest || !capacity || !t7_sanitize_model_string(source, input, &changed)) return false;
    if (changed) { snprintf(dest, capacity, "%s", input); return true; }
    return orig_UI_DoModelStringReplacement(client, element, input, dest, capacity);
}
#define PATH_HOOK(name) HOOK(name, int32_t, int64_t parent, const char *path) { \
    return t7_model_path_valid(path) ? orig_##name(parent, path) : 0; }
PATH_HOOK(UI_Model_GetModelFromPath)
PATH_HOOK(UI_Model_GetModelFromPath_0)
PATH_HOOK(UI_Model_CreateModelFromPath)
HOOK(UI_Model_AllocateNode, int32_t, int32_t parent, const char *path, bool persistent) {
    if (!path || strnlen(path, 64) >= 64) return 0;
    // The Windows free-node counter has no known Mac address.
    return orig_UI_Model_AllocateNode(parent, path, persistent);
}
HOOK(qmemcpy, int64_t, char *dest, const char *source, int32_t size) {
    if (size < 0 || (!dest && size) || (!source && size)) return 0;
    return orig_qmemcpy(dest, source, size);
}
HOOK(CL_GetConfigString, const char *, int32_t index) {
    const char *value = orig_CL_GetConfigString(index);
    if (!bo3_is_campaign() && (index == 3514 || index == 3627) && value &&
        !strncasecmp(value, "mspreload", 9)) return "";
    return value;
}
HOOK(LobbyMsgRW_PrintMessage, void, void) { }
HOOK(LobbyMsgRW_PrintDebugMessage, int32_t, void) { return 0; }
HOOK(ExecLuaCMD, void, void) { }
HOOK(UI_BrowserOpen, char, int64_t arg) { (void)arg; return 0; }
HOOK(Mods_SubscribeUGC, bool, int64_t arg) { (void)arg; return false; }
// T7Patch makes Steam lobbies friends-only separately from this opt-in filter.
static bool invite_allowed(uint64_t id) {
    return !get_friends_only() || id == steam_id().id || steam_friends_with_user(id);
}
HOOK(LiveInvites_AcceptInvite, void, unsigned client, uint32_t *message, uint64_t id) {
    if (invite_allowed(id)) orig_LiveInvites_AcceptInvite(client, message, id);
}
HOOK(LiveInvites_JoinMessageAction, void, unsigned client, uint32_t *message, uint64_t id) {
    if (invite_allowed(id)) orig_LiveInvites_JoinMessageAction(client, message, id);
}
HOOK(LiveInvites_SendJoinInfo, int64_t, unsigned client, uint64_t id, int64_t arg) {
    return invite_allowed(id) ? orig_LiveInvites_SendJoinInfo(client, id, arg) : 0;
}

// Replace Windows TLS access with native command argument accessors.
static const char *(*cmd_argv)(int);
static void (*sv_argv)(int, char *, int);
static bool (*lobby_host)(int);
static const char *(*event_string)(unsigned, unsigned);
HOOK(CL_ConnectionlessCMD, bool, int client, void *from, void *message) {
    const char *command = cmd_argv(0);
    return t7_connectionless_allowed(command, bo3_is_campaign()) ?
        orig_CL_ConnectionlessCMD(client, from, message) : true;
}
static bool menu_allowed(char *entity, bool cached) {
    char response[1024] = {0};
    sv_argv(3, response, sizeof(response));
    response[sizeof(response)-1] = 0;
    const char *text = response;
    if (cached) {
        char *end; unsigned long index = strtoul(response, &end, 10);
        if (!response[0] || *end || index >= 256) return false;
        text = event_string(0, (unsigned)index);
    }
    int32_t entity_number = -1;
    if (t7_memory_readable(entity, sizeof(entity_number))) memcpy(&entity_number, entity, sizeof(entity_number));
    return t7_menu_response_allowed(text, entity_number == 0 && lobby_host(1));
}
HOOK(CMD_MenuResponse_f, void, char *entity) {
    if (menu_allowed(entity, false)) orig_CMD_MenuResponse_f(entity);
}
HOOK(CMD_MenuResponseCached_f, void, char *entity) {
    if (menu_allowed(entity, true)) orig_CMD_MenuResponseCached_f(entity);
}
#define INDEX_HOOK(name, limit) HOOK(name, const char *, unsigned instance, unsigned index) { \
    return index < limit ? orig_##name(instance, index) : "bad"; }
INDEX_HOOK(BG_Cache_GetScriptMenuNameForIndex, 64)
INDEX_HOOK(BG_Cache_GetEventStringNameForIndex, 256)
INDEX_HOOK(BG_Cache_GetLocStringNameForIndex, 2048)
INDEX_HOOK(BG_Cache_GetLUIMenuForIndex, 64)
INDEX_HOOK(BG_Cache_GetLUIMenuDataForIndex, 128)
HOOK(CL_SwitchState_Idle_Update, int32_t, void *state) {
    return bo3_is_campaign() ? orig_CL_SwitchState_Idle_Update(state) : 0;
}
HOOK(LiveSteam_InitServer, void, void) { }

static pthread_mutex_t campaign_lock = PTHREAD_MUTEX_INITIALIZER;
HOOK(Scr_GscObjLink, int64_t, int instance, char *object) {
    int64_t result = orig_Scr_GscObjLink(instance, object);
    pthread_mutex_lock(&campaign_lock);
    if (t7_memory_readable(object, 0x40)) {
        uint32_t size; memcpy(&size, object + 0x28, 4);
        if (size >= 0x40 && size <= 64 * 1024 * 1024 && t7_memory_readable(object, size)) {
            size_t offset, length; const uint8_t *replacement;
            if (t7_campaign_patch((uint8_t *)object, size, &offset, &replacement, &length)) {
                int status = DobbyCodePatch(object + offset, (uint8_t *)replacement, (uint32_t)length);
                t7_log("[BO3MacFix] Demon Within damage patch: %s\n", status == 0 ? "applied" : "failed");
            }
        }
    }
    pthread_mutex_unlock(&campaign_lock);
    return result;
}

void t7_install_hooks(void) {
    t7_log("[BO3MacFix " MACFIX_VERSION "] Starting experimental T7 adapters.\n");
    const char *disabled = getenv("BO3MACFIX_DISABLE_OPTIONAL_HOOKS");
    if (disabled && !strcmp(disabled, "1")) {
        t7_log("[BO3MacFix] Optional T7 hooks disabled by configuration.\n"); return;
    }
    unsigned installed = 0, missing = 0;
#define INSTALL(name) do { \
    void *address = t7_resolve_function(#name); \
    if (!address) { ++missing; t7_log("[BO3MacFix] T7 missing symbol: %s\n", #name); } \
    else { int result = DobbyHook(address, (void *)hook_##name, (void **)&orig_##name); \
        t7_log("[BO3MacFix] T7 %s: %s\n", #name, result == 0 ? "active" : "hook failed"); \
        if (!result) ++installed; } \
} while (0)
    INSTALL(LobbyMsgRW_PackageInt);
    INSTALL(LobbyMsgRW_PackageUInt);
    INSTALL(LobbyMsgRW_PackageUChar);
    INSTALL(SEH_ReplaceDirectiveInStringWithBinding);
    INSTALL(UI_DoModelStringReplacement);
    INSTALL(UI_Model_GetModelFromPath);
    INSTALL(UI_Model_GetModelFromPath_0);
    INSTALL(UI_Model_CreateModelFromPath);
    INSTALL(UI_Model_AllocateNode);
    INSTALL(qmemcpy);
    INSTALL(CL_GetConfigString);
    INSTALL(LobbyMsgRW_PrintMessage);
    INSTALL(LobbyMsgRW_PrintDebugMessage);
    INSTALL(ExecLuaCMD);
    INSTALL(UI_BrowserOpen);
    INSTALL(Mods_SubscribeUGC);
    INSTALL(LiveInvites_AcceptInvite);
    INSTALL(LiveInvites_JoinMessageAction);
    INSTALL(LiveInvites_SendJoinInfo);
    INSTALL(Scr_GscObjLink);
    INSTALL(CL_SwitchState_Idle_Update);
    INSTALL(LiveSteam_InitServer);
    INSTALL(BG_Cache_GetScriptMenuNameForIndex);
    INSTALL(BG_Cache_GetEventStringNameForIndex);
    INSTALL(BG_Cache_GetLocStringNameForIndex);
    INSTALL(BG_Cache_GetLUIMenuForIndex);
    INSTALL(BG_Cache_GetLUIMenuDataForIndex);
    cmd_argv = (void *)t7_resolve_function("Cmd_Argv");
    if (cmd_argv) { INSTALL(CL_ConnectionlessCMD); }
    else t7_log("[BO3MacFix] Connectionless filter needs Cmd_Argv.\n");
    sv_argv = (void *)t7_resolve_function("SV_Cmd_ArgvBuffer");
    lobby_host = (void *)t7_resolve_function("LobbyHost_IsHost");
    event_string = (void *)t7_resolve_function("BG_Cache_GetEventStringNameForIndex");
    if (sv_argv && lobby_host) {
        INSTALL(CMD_MenuResponse_f);
        if (event_string) { INSTALL(CMD_MenuResponseCached_f); }
    } else t7_log("[BO3MacFix] Menu filters need SV_Cmd_ArgvBuffer and LobbyHost_IsHost.\n");
    t7_log("[BO3MacFix] T7 optional hooks: %u installed, %u unresolved.\n", installed, missing);
    t7_install_lua_symbols();
    t7_init_lobby_inspector();
}
