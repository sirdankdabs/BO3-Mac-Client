// Adapted from T7Patch 46b268a: Protection.cpp, Hooks.cpp, CampaignFixes.cpp.
#include "t7_protection.h"
#include <string.h>
#include <strings.h>

bool t7_p2p_allowed(const uint8_t *data, size_t size, bool friends_only, bool friend_sender) {
    if (!data) return false;
    if (size <= 5) return true; // Other Steam/game traffic: preserve upstream behavior.
    switch (data[5]) {
        case 0x65: case 0x66: case 0x6d: return false;
        case 0x68: return !friends_only || friend_sender;
        default: return true;
    }
}

void t7_sanitize_chat(char *data, size_t size) {
    if (!data) return;
    for (size_t i = 0; i < size && data[i]; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c < 0x20 || c > 126 || c == '^' || c == '%' ||
            (i + 1 < size && ((c == '$' && data[i + 1] == '(') ||
                              (c == '[' && data[i + 1] == '{'))))
            data[i] = '.';
    }
}

bool t7_model_path_valid(const char *path) {
    if (!path) return false;
    size_t length = strnlen(path, 4096), key = 0;
    if (length == 4096) return false;
    for (size_t i = 0; i < length; ++i) {
        if (path[i] == '.') key = 0;
        else if (++key >= 64) return false;
    }
    return true;
}

bool t7_sanitize_directives(const char *source, char output[4096]) {
    if (!source) source = "";
    size_t length = strnlen(source, 4096);
    if (length == 4096) return false;
    memcpy(output, source, length + 1);
    for (size_t i = 0; i + 1 < length; ++i) {
        if (output[i] != '^' || output[i + 1] != 'B') continue;
        char *end = strchr(output + i + 2, '^');
        if (!end || end - (output + i + 2) >= 64) output[i++] = '.';
        else i = (size_t)(end - output);
    }
    char *token = strstr(output, "[{");
    while (token) {
        char *end = strstr(token + 2, "}]");
        if (!end || end - token >= 256) token[0] = token[1] = '.';
        token = strstr(token + 2, "[{");
    }
    return true;
}

bool t7_sanitize_model_string(const char *source, char output[4096], bool *changed) {
    if (!source || !changed) return false;
    size_t length = strnlen(source, 4096);
    if (length == 4096) return false;
    memcpy(output, source, length + 1);
    *changed = false;
    for (size_t i = 0; i + 1 < length; ++i) {
        if (output[i] != '$' || output[i + 1] != '(') continue;
        char *end = strchr(output + i + 2, ')');
        if (!end) { output[i] = output[i + 1] = '.'; *changed = true; ++i; }
        else i = (size_t)(end - output);
    }
    return true;
}

bool t7_lobby_value_valid(const char *key, int64_t value) {
    if (!key) return false;
    if (!strcasecmp(key, "lobbytype") || !strcasecmp(key, "srclobbytype") ||
        !strcasecmp(key, "destlobbytype")) return value >= 0 && value <= 2;
    if (!strcasecmp(key, "membercount") || !strcasecmp(key, "clientcount") || !strcasecmp(key, "maxclients"))
        return value >= 0 && value <= 18;
    if (!strcasecmp(key, "votecount")) return value >= 0 && value <= 216;
    if (!strcasecmp(key, "settingssize")) return value > 0 && value <= 0xc000;
    return true;
}

static uint32_t read32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t read16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
bool t7_campaign_patch(const uint8_t *data, size_t size, size_t *offset,
                       const uint8_t **replacement, size_t *length) {
    static const uint8_t magic[] = {0x80,0x47,0x53,0x43,0x0d,0x0a,0};
    static const uint8_t old1b[] = {0xd9,0xca,0xb7,0x6e,0xe2,5,0x28};
    static const uint8_t new1b[] = {0x6b,0xf1,0x90,0x3a,0x5d,8,0x42};
    static const uint8_t old1c[] = {0xd9,0xca,0xb7,0x6e,0xa2,3,5,0x34,0x0d,0x12};
    static const uint8_t new1c[] = {0x6b,0xf1,0x90,0x3a,0xbb,9,8,0x51,0x53,0x0e};
    if (!data || !offset || !replacement || !length || size < 0x40 || memcmp(data, magic, 7)) return false;
    bool v1b = data[7] == 0x1b;
    if (!v1b && data[7] != 0x1c) return false;
    uint32_t declared = read32(data + 0x28), table = read32(data + 0x20);
    uint16_t count = read16(data + 0x3a);
    if (declared < 0x40 || declared > size || declared > 64 * 1024 * 1024 ||
        count > 4096 || table > declared || (size_t)count * 0x14 > declared - table) return false;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t *entry = data + table + (size_t)i * 0x14;
        if (read32(entry + 8) != 0x56f9ab2e || read32(entry + 12) != 0xb0a87e94 ||
            read32(entry) != (v1b ? 0xc5ba252d : 0x6ba4a2e4)) continue;
        uint32_t code = read32(entry + 4), minimum = v1b ? 0xd5 : 0xfa;
        if (code > declared || minimum > declared - code) return false;
        size_t at = code + (v1b ? 0xb4 : 0xc4), n = v1b ? sizeof(old1b) : sizeof(old1c);
        if (memcmp(data + at, v1b ? old1b : old1c, n)) return false;
        *offset = at; *replacement = v1b ? new1b : new1c; *length = n;
        return true;
    }
    return false;
}

bool t7_connectionless_allowed(const char *command, bool campaign) {
    static const char *allowed[] = {"connectResponse", "statresponse", "LM", "disconnect",
        "loadoutResponse", "infoResponse", "statusResponse", "keyAuthorize", "error", "print",
        "fastrestart", "ping", "pinga", "steamAuthReq", "cfl"};
    if (!command) return false;
    if (campaign && (!strcasecmp(command, "requeststats") || !strcasecmp(command, "requeststats\n"))) return true;
    for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); ++i)
        if (!strcmp(command, allowed[i])) return true;
    return false;
}

bool t7_menu_response_allowed(const char *response, bool local_host) {
    if (!response || !strcasecmp(response, "badspawn")) return false;
    if (!strcasecmp(response, "killserverpc") || !strcasecmp(response, "endgame") ||
        !strcasecmp(response, "endround") || !strcasecmp(response, "restart_level_zm")) return local_host;
    return true;
}
