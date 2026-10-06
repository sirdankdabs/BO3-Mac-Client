#include "t7_runtime.h"
#include "t7_protection.h"
#include "steam.h"
#include "dobby.h"
#include "versioninfo.h"
#include "t7_ranked_sites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <mach-o/dyld.h>

// Only functions resolved from the Mac executable are installed. These native
// signatures use the SysV ABI; no Win32 calling conventions or RVAs are used.
#define MAC_ADDR(rva) ((uintptr_t)(rva) + 0x100000000ull + (uintptr_t)_dyld_get_image_vmaddr_slide(0))
#define HOOK(name, result, ...) static result (*orig_##name)(__VA_ARGS__); static result hook_##name(__VA_ARGS__)
HOOK(LobbyMsgRW_PackageInt, bool, void *msg, const char *key, int32_t *value) {
    if (!value || !key || !orig_LobbyMsgRW_PackageInt(msg, key, value)) return false;
    if (!strcasecmp(key, "datamask") && *value == 0) return false; // T7Patch 3.09
    return t7_lobby_value_valid(key, *value);
}
HOOK(LobbyMsgRW_PackageUInt, bool, void *msg, const char *key, uint32_t *value) {
    // The Mac build serializes "datamask" through this wrapper (type 1).
    // T7Patch 3.09 rejects a zero datamask; the Windows session-state check
    // that follows it relies on a global with no verified Mac address.
    if (!value || !key || !orig_LobbyMsgRW_PackageUInt(msg, key, value)) return false;
    if (!strcasecmp(key, "datamask") && *value == 0) return false;
    return t7_lobby_value_valid(key, *value);
}
HOOK(LobbyMsgRW_PackageUChar, bool, void *msg, const char *key, uint8_t *value) {
    if (!value || !key || !orig_LobbyMsgRW_PackageUChar(msg, key, value)) return false;
    if (!strcasecmp(key, "nattype") && *value > 4) *value = 4;
    return true;
}
// SEH_ReplaceDirectiveInStringWithBinding (0xf47799) and two other Mac callers
// copy the string into a buffer and call these parsers in a loop with a
// cursor. Sanitize the whole buffer once, on the first call (cursor 0), with
// T7Patch's rules; replacements keep the length unchanged.
static void sanitize_directive_buffer(const int32_t *cursor, char *buffer) {
    char clean[4096];
    if (!cursor || !buffer || *cursor != 0) return;
    if (t7_sanitize_directives(buffer, clean)) memcpy(buffer, clean, strlen(clean) + 1);
    else buffer[0] = 0; // longer than any caller's buffer: drop it
}
HOOK(SEH_ParseNextDirective, bool, int client, int32_t *cursor, int32_t *length, char *buffer, int32_t size) {
    sanitize_directive_buffer(cursor, buffer);
    return orig_SEH_ParseNextDirective(client, cursor, length, buffer, size);
}
HOOK(SEH_ParseNextDirectiveShort, bool, int client, int32_t *cursor, int32_t *length, char *buffer) {
    sanitize_directive_buffer(cursor, buffer);
    return orig_SEH_ParseNextDirectiveShort(client, cursor, length, buffer);
}
HOOK(UI_DoModelStringReplacement, bool, int client, char *element, const char *source, char *dest, unsigned capacity) {
    char input[4096]; bool changed;
    if (!dest || !capacity || !t7_sanitize_model_string(source, input, &changed)) return false;
    if (changed) { snprintf(dest, capacity, "%s", input); return true; }
    return orig_UI_DoModelStringReplacement(client, element, input, dest, capacity);
}
// GetModelFromPath / CreateModelFromPath all tail-call this walker, which
// copies each '.'-separated key into a 64-byte stack buffer without a bound.
HOOK(UI_Model_ResolvePath, uint16_t, uint16_t parent, const char *path, bool create, bool persistent) {
    return t7_model_path_valid(path) ? orig_UI_Model_ResolvePath(parent, path, create, persistent) : 0;
}
// The Mac allocator already rejects keys >= 64; T7Patch also returns 0 instead
// of raising EXE_LUI_OUT_OF_MEMORY when the free list (head at 0x431d728) is empty.
HOOK(UI_Model_AllocateNode, uint16_t, uint16_t parent, const char *path, bool persistent) {
    if (!path || strnlen(path, 64) >= 64) return 0;
    if (t7_known_mac_build() && !*(volatile uint16_t *)MAC_ADDR(0x431d728)) return 0;
    return orig_UI_Model_AllocateNode(parent, path, persistent);
}
// T7Patch: index < -1 or past MESSAGE_TYPE_DEMO_STATE (0x20) is "invalid".
// The Mac table at 0x1b78cf0 has exactly those 33 names.
HOOK(LobbyTypes_GetMsgTypeName, const char *, int32_t index) {
    if (index < -1 || index > 0x20) return "invalid";
    return orig_LobbyTypes_GetMsgTypeName(index);
}
// Mac LivePresence_Serialize(unpack, presence, buffer, size): byte 1 of the
// buffer is the control byte whose bits 2-6 hold the player count. Clamp it to
// 18 before unpacking, like T7Patch.
HOOK(LivePresence_Serialize, int32_t, bool unpack, void *presence, uint8_t *buffer, uint64_t size) {
    if (unpack && buffer && size >= 2 && ((buffer[1] >> 2) & 0x1f) > 18)
        buffer[1] = (uint8_t)((buffer[1] & ~(0x1f << 2)) | (18 << 2));
    return orig_LivePresence_Serialize(unpack, presence, buffer, size);
}
HOOK(CL_GetConfigString, const char *, int32_t index) {
    const char *value = orig_CL_GetConfigString(index);
    if (!bo3_is_campaign() && (index == 3514 || index == 3627) && value &&
        !strncasecmp(value, "mspreload", 9)) return "";
    return value;
}
HOOK(LobbyMsgRW_PrintMessage, void, void *msg, bool force) { (void)msg; (void)force; }
// Mac LobbyMsgRW reads nested "Start Debug Tag" (0xf) elements recursively, so a
// crafted message can nest them until the stack overflows. Cap the depth and
// mark the message overflowed (msg_t.overflowed is the first int32), which
// every caller treats as a failed read.
static __thread int debug_section_depth;
HOOK(LobbyMsgRW_ReadDebugSection, int32_t, int32_t *msg) {
    if (debug_section_depth >= 16) {
        if (msg) *msg = 1;
        return 0x10; // "End Debug Tag": unwinds the caller's loop
    }
    ++debug_section_depth;
    int32_t type = orig_LobbyMsgRW_ReadDebugSection(msg);
    --debug_section_depth;
    return type;
}
HOOK(UI_BrowserOpen, char, int64_t arg) { (void)arg; return 0; }
HOOK(Mods_SubscribeUGC, bool, int64_t arg) {
    const char *enabled = getenv("BO3MACFIX_AUTOINSTALLMODS");
    return enabled && !strcmp(enabled, "1") ? orig_Mods_SubscribeUGC(arg) : false;
}
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

// Mac CL_ConnectionlessCMD(localClient, netadr_t from /* 16 bytes, two regs */, msg_t *msg)
// tokenizes the packet itself, so Cmd_Argv is not valid on entry. Read the
// command from a private copy of msg_t with the game's own MSG readers.
// Mac msg_t: +0 overflowed, +8 data, +0x10 splitData, +0x1c cursize,
// +0x20 splitSize, +0x24 readcount, +0x28 bit.
static void (*MSG_BeginReading)(void *msg);
static int32_t (*MSG_ReadLong)(void *msg);
static char *(*MSG_ReadStringLine)(void *msg, char *buffer, int size);
HOOK(CL_ConnectionlessCMD, bool, int client, uint64_t from_lo, uint64_t from_hi, void *msg) {
    uint8_t copy[0x2c]; char line[1024], command[64];
    if (!t7_memory_readable(msg, sizeof(copy))) return true;
    memcpy(copy, msg, sizeof(copy));
    MSG_BeginReading(copy);
    MSG_ReadLong(copy);
    MSG_ReadStringLine(copy, line, sizeof(line));
    const char *p = line; size_t n = 0;
    while (*p == ' ' || *p == '\t') ++p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && n < sizeof(command) - 1) command[n++] = *p++;
    command[n] = 0;
    // Voice/relay packets ("v", "vt", "relay", "RA") are consumed by the Mac
    // client before its command dispatch, so they stay allowed.
    if (!strcmp(command, "v") || !strcmp(command, "vt") || !strcmp(command, "relay") || !strcmp(command, "RA") ||
        t7_connectionless_allowed(command, bo3_is_campaign()))
        return orig_CL_ConnectionlessCMD(client, from_lo, from_hi, msg);
    return true;
}

// T7Patch 3.09 menu-response policy on top of the Mac handler, which already
// checks the server id and bans a non-host lobby trying to end the game.
static void (*sv_argv)(int, char *, int);
static void (*cbuf_add_text)(int, const char *);
static unsigned menu_entity_number(char *entity) {
    // Same computation as the Mac handler's own banClient path.
    uintptr_t base = *(uintptr_t *)MAC_ADDR(0x1966be8);
    return (unsigned)(((uintptr_t)entity - base) >> 3) * 0x2eb11b5fu;
}
HOOK(CMD_MenuResponse_f, void, char *entity) {
    const int32_t *args = (const int32_t *)MAC_ADDR(0x125dc3c0); // sv_cmd_args
    if (args[*args + 0x64 / 4] == 4) {
        char response[1024] = {0};
        sv_argv(3, response, sizeof(response));
        unsigned number = menu_entity_number(entity);
        bool campaign = bo3_is_campaign();
        bool end_game = !strcasecmp(response, "endgame") || !strcasecmp(response, "endround");
        if (number != 0 && (!strcasecmp(response, "killserverpc") || !strcasecmp(response, "restart_level_zm") ||
                            (!campaign && end_game))) {
            char command[64];
            snprintf(command, sizeof(command), "tempBanClient %u\n", number);
            cbuf_add_text(*(int32_t *)MAC_ADDR(0x208f1c8), command);
            return;
        }
    }
    orig_CMD_MenuResponse_f(entity);
}
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

// T7Patch SPOOF_RANKED (from Scropts-QOL): the listed Mods_IsModsLoaded call
// sites answer "no mods", so XP/stats keep counting with a mod loaded. Instead
// of filtering return addresses inside a hook, each verified "call 0xe4962f"
// is replaced with "xor eax,eax; nopl (rax)". Opt-in only.
static void t7_install_ranked_spoof(void) {
    const char *enabled = getenv("BO3MACFIX_SPOOF_RANKED");
    if (!enabled || strcmp(enabled, "1")) return;
    if (!t7_known_mac_build()) {
        t7_log("[BO3MacFix] Ranked spoof: skipped, executable is not the known Mac build.\n"); return;
    }
    static const uint8_t no_mods[5] = {0x31, 0xc0, 0x0f, 0x1f, 0x00};
    const uintptr_t target = MAC_ADDR(0xe4962f);
    unsigned patched = 0;
    for (size_t i = 0; i < sizeof(t7_ranked_sites) / sizeof(t7_ranked_sites[0]); ++i) {
        uint8_t *site = (uint8_t *)MAC_ADDR(t7_ranked_sites[i].rva);
        int32_t rel;
        if (!t7_memory_readable(site, 5) || site[0] != 0xe8) {
            t7_log("[BO3MacFix] Ranked spoof %s: unexpected bytes, skipped\n", t7_ranked_sites[i].what); continue;
        }
        memcpy(&rel, site + 1, 4);
        if ((uintptr_t)(site + 5 + rel) != target) {
            t7_log("[BO3MacFix] Ranked spoof %s: call target mismatch, skipped\n", t7_ranked_sites[i].what); continue;
        }
        if (DobbyCodePatch(site, (uint8_t *)no_mods, sizeof(no_mods)) == 0) ++patched;
        else t7_log("[BO3MacFix] Ranked spoof %s: patch failed\n", t7_ranked_sites[i].what);
    }
    t7_log("[BO3MacFix] Ranked spoof: %u/%zu call sites patched.\n", patched,
           sizeof(t7_ranked_sites) / sizeof(t7_ranked_sites[0]));
}

void t7_install_hooks(void) {
    t7_log("[BO3MacFix " MACFIX_VERSION "] Starting experimental T7 adapters.\n");
    t7_install_ranked_spoof();
    const char *disabled = getenv("BO3MACFIX_DISABLE_OPTIONAL_HOOKS");
    if (disabled && !strcmp(disabled, "1")) {
        t7_log("[BO3MacFix] Optional T7 hooks disabled by configuration.\n"); return;
    }
    unsigned installed = 0, missing = 0;
#define INSTALL(name) do { \
    void *address = t7_resolve_function(#name); \
    if (!address) { ++missing; t7_log("[BO3MacFix] T7 %s: not mapped for this executable\n", #name); } \
    else { int result = DobbyHook(address, (void *)hook_##name, (void **)&orig_##name); \
        t7_log("[BO3MacFix] T7 %s: %s\n", #name, result == 0 ? "active" : "hook failed"); \
        if (!result) ++installed; } \
} while (0)
    INSTALL(LobbyMsgRW_PackageInt);
    INSTALL(LobbyMsgRW_PackageUInt);
    INSTALL(LobbyMsgRW_PackageUChar);
    INSTALL(LobbyMsgRW_PrintMessage);
    INSTALL(LobbyMsgRW_ReadDebugSection);
    INSTALL(LobbyTypes_GetMsgTypeName);
    INSTALL(SEH_ParseNextDirective);
    INSTALL(SEH_ParseNextDirectiveShort);
    INSTALL(UI_DoModelStringReplacement);
    INSTALL(UI_Model_ResolvePath);
    INSTALL(UI_Model_AllocateNode);
    INSTALL(CL_GetConfigString);
    INSTALL(UI_BrowserOpen);
    INSTALL(Mods_SubscribeUGC);
    INSTALL(LiveInvites_AcceptInvite);
    INSTALL(LiveInvites_JoinMessageAction);
    INSTALL(LiveInvites_SendJoinInfo);
    INSTALL(LivePresence_Serialize);
    INSTALL(Scr_GscObjLink);
    INSTALL(CL_SwitchState_Idle_Update);
    INSTALL(LiveSteam_InitServer);
    // qmemcpy has no Mac equivalent (memcpy is inlined); ExecLuaCMD's crash is
    // already neutralized by upstream's Lua_CmdParseArgs patch.
    // BG_Cache_* index hooks stay disabled, as in upstream T7Patch.
    MSG_BeginReading = t7_resolve_function("MSG_BeginReading");
    MSG_ReadLong = t7_resolve_function("MSG_ReadLong");
    MSG_ReadStringLine = t7_resolve_function("MSG_ReadStringLine");
    if (MSG_BeginReading && MSG_ReadLong && MSG_ReadStringLine) { INSTALL(CL_ConnectionlessCMD); }
    else t7_log("[BO3MacFix] Connectionless filter needs the MSG readers.\n");
    // The menu filter also reads globals, so it needs the exact known build.
    sv_argv = t7_resolve_function("SV_Cmd_ArgvBuffer");
    cbuf_add_text = t7_resolve_function("Cbuf_AddText");
    if (sv_argv && cbuf_add_text && t7_known_mac_build()) { INSTALL(CMD_MenuResponse_f); }
    else t7_log("[BO3MacFix] Menu filter needs the known Mac build.\n");
    t7_log("[BO3MacFix] T7 hooks: %u installed, %u unresolved.\n", installed, missing);
    t7_install_lua_symbols();
    t7_init_lobby_inspector();
}
