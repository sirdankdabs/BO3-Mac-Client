#include "steam.h"
#include "t7_runtime.h"
#include "t7_protection.h"
#include "dobby.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stable legacy Steam interface slots, also used by T7Patch's vtable hooks.
// Verified against Valve's public ISteamNetworking / ISteamMatchmaking headers.
static bool (*read_packet)(void *, void *, uint32_t, uint32_t *, CSteamID *, int);
static SteamAPICall_t (*create_lobby)(void *, int, int);
static int (*read_chat)(void *, CSteamID, int, CSteamID *, void *, int, int *);
static int (*member_count)(void *, CSteamID);
static CSteamID (*member_at)(void *, CSteamID, int);

static bool guarded_read(void *self, void *data, uint32_t capacity, uint32_t *size, CSteamID *sender, int channel) {
    if (!data || !size || !sender) return false;
    bool result = read_packet(self, data, capacity, size, sender, channel);
    if (!result) return false;
    bool friend_sender = true;
    if (*size > capacity) { *size = 0; return false; }
    if (*size > 5 && ((uint8_t *)data)[5] == 0x68 && get_friends_only())
        friend_sender = sender->id == steam_id().id || steam_friends_with_user(sender->id);
    if (!t7_p2p_allowed(data, *size, get_friends_only(), friend_sender)) {
        *size = 0;
        return false;
    }
    return true;
}
static SteamAPICall_t guarded_create(void *self, int type, int maximum) {
    (void)type;
    // ELobbyTypeFriendsOnly, matching T7Patch::CreateLobby.
    return create_lobby(self, 1, maximum);
}
static int guarded_chat(void *self, CSteamID lobby, int chat_id, CSteamID *sender,
                        void *data, int capacity, int *type) {
    if (!data || capacity <= 0) return 0;
    CSteamID local_sender = {0}; int local_type = 0;
    if (!sender) sender = &local_sender;
    if (!type) type = &local_type;
    int count = read_chat(self, lobby, chat_id, sender, data, capacity, type);
    if (count <= 0) return count;
    if (count > capacity) { ((char *)data)[0] = 0; return 0; }
    // Uses Steam lobby membership, not Windows-only BO3 session structures.
    bool member = false;
    int members = member_count(self, lobby);
    if (members >= 0 && members <= 250) {
        for (int i = 0; i < members; ++i)
            if (member_at(self, lobby, i).id == sender->id) { member = true; break; }
    }
    if (!member) { ((char *)data)[0] = 0; return 0; }
    if (*type == 1) {
        t7_sanitize_chat(data, (size_t)count);
        // Guarantee termination within the returned buffer for game string use.
        if (!memchr(data, 0, (size_t)count)) ((char *)data)[count - 1] = 0;
    }
    return count;
}
static void patch_slot(void **slot, void *hook, void **original, const char *name) {
    if (*slot == hook) return;
    if (!*slot) return;
    *original = *slot;
    int status = DobbyCodePatch(slot, (uint8_t *)&hook, sizeof(hook));
    t7_log("[BO3MacFix] Steam %s: %s\n", name, status == 0 ? "active" : "patch failed");
}
void steam_install_extra_hooks(CSteamAPIContext *context) {
    const char *disabled = getenv("BO3MACFIX_DISABLE_STEAM_FILTERS");
    if (disabled && !strcmp(disabled, "1")) return;
    if (context->m_pSteamNetworking) {
        void **table = *(void ***)context->m_pSteamNetworking;
        if (table) patch_slot(&table[2], guarded_read, (void **)&read_packet, "ReadP2PPacket");
    }
    if (context->m_pSteamMatchmaking) {
        void **table = *(void ***)context->m_pSteamMatchmaking;
        if (!table) return;
        member_count = (void *)table[17];
        member_at = (void *)table[18];
        patch_slot(&table[13], guarded_create, (void **)&create_lobby, "CreateLobby");
        if (member_count && member_at)
            patch_slot(&table[27], guarded_chat, (void **)&read_chat, "GetLobbyChatEntry");
    }
}
