// Joining BOIII/Ezz community dedicated servers from the Mac build (opt-in,
// BO3MACFIX_BOIII=1). See docs/BOIII-MAC-PORT.md for the protocol analysis.
//
// Flow ("boiii_connect ip:port" in the console, or BO3MACFIX_BOIII_CONNECT):
//  1. getInfo on a private socket; require protocol 8, gamename T7 and a stock
//     map (no workshop content yet).
//  2. On the main thread, CL_LaunchClient(map, gametype) starts the game's own
//     client connection (as a lobby client would). CL_Connect aims it at
//     "localhost"; CL_CheckForResend is hooked to retarget it to the server.
//  3. While CHALLENGING we send "getchallenge"; the server answers
//     "getChallengeResponse <32 bytes>" and we move to CONNECTING.
//  4. The game's "connect" packet is rewrapped like BOIII does: ECC P-521
//     public key, signature over SHA-512(challenge), profile info, and the
//     userinfo with the xuid replaced by the key-derived xuid, sent in
//     0x400-byte fragments.
// Nothing here touches Steam ownership checks.
#include "t7_runtime.h"
#include "dobby.h"
#include "dyld-interposing.h"
#define LTM_DESC
#include "tomcrypt/tomcrypt.h"
#include <arpa/inet.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MAC_ADDR(rva) ((uintptr_t)(rva) + 0x100000000ull + (uintptr_t)_dyld_get_image_vmaddr_slide(0))
#define CONNSTATE_CHALLENGING 5
#define CONNSTATE_CONNECTING 6

static bool enabled(void) {
    const char *value = getenv("BO3MACFIX_BOIII");
    return value && !strcmp(value, "1");
}

static uint64_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

// ---------------------------------------------------------------- identity --
static ecc_key key;
static bool key_ready;
static uint64_t key_xuid;
static unsigned char public_key[512];
static unsigned long public_key_length;
static int prng_index = -1;

static void file_next_to_dylib(const char *name, char *out, size_t capacity) {
    Dl_info info;
    out[0] = 0;
    if (!dladdr((const void *)file_next_to_dylib, &info) || !info.dli_fname) return;
    snprintf(out, capacity, "%s", info.dli_fname);
    char *slash = strrchr(out, '/');
    if (slash) snprintf(slash + 1, capacity - (size_t)(slash + 1 - out), "%s", name);
}

static bool load_or_create_key(void) {
    if (key_ready) return true;
    ltc_mp = ltm_desc;
    prng_index = register_prng(&sprng_desc);
    register_hash(&sha1_desc);
    register_hash(&sha512_desc);
    char path[1024];
    file_next_to_dylib("boiii_key.dat", path, sizeof(path));
    unsigned char stored[1024];
    size_t stored_length = 0;
    FILE *file = fopen(path, "rb");
    if (file) { stored_length = fread(stored, 1, sizeof(stored), file); fclose(file); }
    if (!stored_length || ecc_import(stored, stored_length, &key) != CRYPT_OK) {
        // Same parameters as BOIII: 64-byte key size -> P-521.
        if (ecc_make_key(NULL, prng_index, 64, &key) != CRYPT_OK) {
            t7_log("[BO3MacFix] BOIII: key generation failed\n"); return false;
        }
        unsigned long length = sizeof(stored);
        if (ecc_export(stored, &length, PK_PRIVATE, &key) == CRYPT_OK && (file = fopen(path, "wb"))) {
            fwrite(stored, 1, length, file); fclose(file);
        }
        t7_log("[BO3MacFix] BOIII: created identity key %s\n", path);
    }
    public_key_length = sizeof(public_key);
    unsigned char x963[256], digest[20];
    unsigned long x963_length = sizeof(x963), digest_length = sizeof(digest);
    if (ecc_export(public_key, &public_key_length, PK_PUBLIC, &key) != CRYPT_OK ||
        ecc_ansi_x963_export(&key, x963, &x963_length) != CRYPT_OK ||
        hash_memory(find_hash("sha1"), x963, x963_length, digest, &digest_length) != CRYPT_OK) {
        t7_log("[BO3MacFix] BOIII: key export failed\n"); return false;
    }
    memcpy(&key_xuid, digest, sizeof(key_xuid)); // BOIII: first 8 bytes of SHA-1
    key_ready = true;
    t7_log("[BO3MacFix] BOIII: identity xuid %llx\n", (unsigned long long)key_xuid);
    return true;
}

// ------------------------------------------------------------------ target --
static struct {
    volatile int active;      // retarget CL_Connect to this server
    volatile int launch;      // main thread should call CL_LaunchClient
    uint8_t ip[4];
    uint16_t port;
    char map[64], gametype[64], printable[64];
    uint64_t started_ms;
} target;
static uint8_t challenge[32];
static volatile int have_challenge;
static uint64_t last_challenge_request;

static uint8_t *client_server_address(int client) {
    uintptr_t base = *(uintptr_t *)MAC_ADDR(0x115345a0);
    return base ? (uint8_t *)(base + (uintptr_t)client * 0x25780 + 0x10) : NULL;
}
static int32_t *client_state(int client) {
    return (int32_t *)MAC_ADDR(0x115324a8 + (uintptr_t)client * 0x1078);
}
static bool is_target(uint64_t address_lo) {
    uint8_t bytes[8];
    memcpy(bytes, &address_lo, sizeof(bytes));
    uint16_t port;
    memcpy(&port, bytes + 4, 2);
    return !memcmp(bytes, target.ip, 4) && port == target.port;
}

// ------------------------------------------------------------- game hooks --
static void (*NET_OutOfBandPrint)(int, uint64_t, uint64_t, const char *);
static int (*CL_GetNetSrc)(int);
static bool (*CL_LaunchClient)(const char *, const char *, const char **);

static void (*orig_CL_CheckForResend)(int);
static void hook_CL_CheckForResend(int client) {
    if (target.active && client == 0) {
        uint8_t *address = client_server_address(0);
        int32_t *state = client_state(0);
        if (address && (*state == CONNSTATE_CHALLENGING || *state == CONNSTATE_CONNECTING)) {
            memcpy(address, target.ip, 4);
            memcpy(address + 4, &target.port, 2);
            int32_t ip_type = 4; // NA_IP on the Mac build
            memcpy(address + 8, &ip_type, 4);
            if (!have_challenge) {
                *state = CONNSTATE_CHALLENGING;
                uint64_t now = now_ms();
                if (now - last_challenge_request >= 1000) {
                    last_challenge_request = now;
                    uint64_t lo, hi;
                    memcpy(&lo, address, 8); memcpy(&hi, address + 8, 8);
                    NET_OutOfBandPrint(CL_GetNetSrc(0), lo, hi, "getchallenge");
                    t7_log("[BO3MacFix] BOIII: getchallenge -> %s\n", target.printable);
                }
                return;
            }
        }
    }
    orig_CL_CheckForResend(client);
}

static void put(uint8_t *buffer, size_t *length, const void *data, size_t size) {
    memcpy(buffer + *length, data, size); *length += size;
}
static void put_string(uint8_t *buffer, size_t *length, const void *data, uint32_t size) {
    put(buffer, length, &size, 4); put(buffer, length, data, size);
}

// Rebuilds 'connect "<info>"' with xuid replaced by the key-derived xuid.
static bool rewrite_connect(const char *data, int length, char *out, size_t capacity) {
    if (length < 10 || length > 1500 || memcmp(data, "connect \"", 9)) return false;
    char info[1600];
    int info_length = length - 9;
    memcpy(info, data + 9, (size_t)info_length);
    info[info_length] = 0;
    char *quote = strrchr(info, '"');
    if (quote) *quote = 0;
    size_t used = (size_t)snprintf(out, capacity, "connect \"");
    char *cursor = info + (info[0] == '\\');
    bool wrote_xuid = false;
    while (cursor && *cursor) {
        char *name = cursor, *value = strchr(name, '\\');
        if (!value) break;
        *value++ = 0;
        char *next = strchr(value, '\\');
        if (next) *next++ = 0;
        if (!strcmp(name, "xuid")) {
            used += (size_t)snprintf(out + used, capacity - used, "\\xuid\\%llx", (unsigned long long)key_xuid);
            wrote_xuid = true;
        } else {
            used += (size_t)snprintf(out + used, capacity - used, "\\%s\\%s", name, value);
        }
        if (used >= capacity) return false;
        cursor = next;
    }
    if (!wrote_xuid) used += (size_t)snprintf(out + used, capacity - used, "\\xuid\\%llx", (unsigned long long)key_xuid);
    used += (size_t)snprintf(out + used, capacity - used, "\"");
    return used < capacity;
}

static bool (*orig_NET_OutOfBandData)(int, uint64_t, uint64_t, const uint8_t *, int);
static bool hook_NET_OutOfBandData(int socket_id, uint64_t lo, uint64_t hi, const uint8_t *data, int length) {
    if (!target.active || !have_challenge || !key_ready || !data || !is_target(lo))
        return orig_NET_OutOfBandData(socket_id, lo, hi, data, length);
    char connect[1700];
    if (!rewrite_connect((const char *)data, length, connect, sizeof(connect)))
        return orig_NET_OutOfBandData(socket_id, lo, hi, data, length);

    unsigned char digest[64], signature[256];
    unsigned long digest_length = sizeof(digest), signature_length = sizeof(signature);
    ltc_ecc_sig_opts options;
    memset(&options, 0, sizeof(options));
    options.type = LTC_ECCSIG_ANSIX962;
    options.wprng = prng_index;
    if (hash_memory(find_hash("sha512"), challenge, sizeof(challenge), digest, &digest_length) != CRYPT_OK ||
        ecc_sign_hash_v2(digest, digest_length, signature, &signature_length, &options, &key) != CRYPT_OK) {
        t7_log("[BO3MacFix] BOIII: signing failed\n");
        return false;
    }
    static uint8_t payload[4096];
    size_t payload_length = 0;
    int32_t profile_version = 3; // BOIII profile_info, empty DDL
    put_string(payload, &payload_length, public_key, (uint32_t)public_key_length);
    put_string(payload, &payload_length, signature, (uint32_t)signature_length);
    put(payload, &payload_length, &profile_version, 4);
    put_string(payload, &payload_length, "", 0);
    put_string(payload, &payload_length, connect, (uint32_t)strlen(connect));

    static uint64_t fragment_id;
    const uint64_t id = fragment_id++;
    const uint32_t count = (uint32_t)((payload_length + 0x3ff) / 0x400);
    bool sent = true;
    for (uint32_t index = 0; index < count; ++index) {
        uint8_t packet[0x420];
        size_t packet_length = 0;
        size_t offset = (size_t)index * 0x400, chunk = payload_length - offset > 0x400 ? 0x400 : payload_length - offset;
        put(packet, &packet_length, "connect ", 8);
        put(packet, &packet_length, &id, 8);
        put(packet, &packet_length, &count, 4);
        put(packet, &packet_length, &index, 4);
        put(packet, &packet_length, payload + offset, chunk);
        sent &= orig_NET_OutOfBandData(socket_id, lo, hi, packet, (int)packet_length);
    }
    t7_log("[BO3MacFix] BOIII: signed connect sent (%zu bytes, %u fragment(s))\n", payload_length, count);
    return sent;
}

// Called from the T7 connectionless filter before it decides. Returns true when
// the packet was consumed here.
bool boiii_connectionless(const char *command, const void *msg) {
    if (!target.active || !command) return false;
    if (!strcmp(command, "getChallengeResponse")) {
        const uint8_t *data = *(const uint8_t *const *)((const uint8_t *)msg + 8);
        int32_t size;
        memcpy(&size, (const uint8_t *)msg + 0x1c, 4);
        const size_t offset = 4 + sizeof("getChallengeResponse ") - 1;
        if (data && size >= (int32_t)(offset + sizeof(challenge))) {
            memcpy(challenge, data + offset, sizeof(challenge));
            have_challenge = 1;
            int32_t *state = client_state(0);
            if (*state == CONNSTATE_CHALLENGING) *state = CONNSTATE_CONNECTING;
            t7_log("[BO3MacFix] BOIII: challenge received, connecting\n");
        }
        return true;
    }
    if (!strcmp(command, "profileInfo") || !strcmp(command, "playerXuid")) return true;
    if (!strcmp(command, "error") || !strcmp(command, "connectResponse") || !strcmp(command, "print"))
        t7_log("[BO3MacFix] BOIII: server sent '%s'\n", command);
    return false;
}

// --------------------------------------------------------------- networking --
// The game binds its UDP socket to 127.0.0.1 (net_ip "localhost"); internet
// servers need it on all interfaces.
static int bind_hook(int socket_fd, const struct sockaddr *address, socklen_t length) {
    if (enabled() && address && address->sa_family == AF_INET && length >= (socklen_t)sizeof(struct sockaddr_in)) {
        struct sockaddr_in copy;
        memcpy(&copy, address, sizeof(copy));
        int type = 0;
        socklen_t type_length = sizeof(type);
        if (copy.sin_addr.s_addr == htonl(INADDR_LOOPBACK) &&
            !getsockopt(socket_fd, SOL_SOCKET, SO_TYPE, &type, &type_length) && type == SOCK_DGRAM) {
            copy.sin_addr.s_addr = htonl(INADDR_ANY);
            t7_log("[BO3MacFix] BOIII: game UDP socket bound to 0.0.0.0:%u\n", ntohs(copy.sin_port));
            return bind(socket_fd, (const struct sockaddr *)&copy, sizeof(copy));
        }
    }
    return bind(socket_fd, address, length);
}
DYLD_INTERPOSE(bind_hook, bind);

static bool resolve(const char *text, uint8_t ip[4], uint16_t *port) {
    char host[256];
    snprintf(host, sizeof(host), "%s", text);
    unsigned value = 27017;
    char *colon = strrchr(host, ':');
    if (colon) { *colon = 0; value = (unsigned)strtoul(colon + 1, NULL, 10); }
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (!*host || !value || value > 65535 || getaddrinfo(host, NULL, &hints, &result) || !result) return false;
    memcpy(ip, &((struct sockaddr_in *)result->ai_addr)->sin_addr, 4);
    *port = (uint16_t)value; // host order, as NET_StringToAdr stores it
    freeaddrinfo(result);
    return true;
}

static const char *info_value(const char *info, const char *name, char *out, size_t capacity) {
    char pattern[80];
    snprintf(pattern, sizeof(pattern), "\\%s\\", name);
    const char *found = strstr(info, pattern);
    out[0] = 0;
    if (found) {
        found += strlen(pattern);
        size_t length = strcspn(found, "\\");
        if (length >= capacity) length = capacity - 1;
        memcpy(out, found, length);
        out[length] = 0;
    }
    return out;
}

static void *connect_thread(void *argument) {
    char *text = argument;
    uint8_t ip[4]; uint16_t port;
    if (!resolve(text, ip, &port)) {
        t7_log("[BO3MacFix] BOIII: cannot resolve '%s'\n", text); free(text); return NULL;
    }
    struct sockaddr_in to = {0};
    to.sin_family = AF_INET; to.sin_port = htons(port); memcpy(&to.sin_addr, ip, 4);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char query[64], reply[4096];
    int query_length = snprintf(query, sizeof(query), "\xff\xff\xff\xffgetInfo\n%u", (unsigned)arc4random());
    sendto(sock, query, (size_t)query_length, 0, (struct sockaddr *)&to, sizeof(to));
    ssize_t received = recvfrom(sock, reply, sizeof(reply) - 1, 0, NULL, NULL);
    close(sock);
    if (received <= 4) { t7_log("[BO3MacFix] BOIII: %s did not answer getInfo\n", text); free(text); return NULL; }
    reply[received] = 0;
    const char *info = strchr(reply + 4, '\n');
    info = info ? info + 1 : "";
    char protocol[16], sub[16], game[16], map[64], gametype[64], workshop[64], usermap[64], host[128];
    info_value(info, "protocol", protocol, sizeof(protocol));
    info_value(info, "sub_protocol", sub, sizeof(sub));
    info_value(info, "gamename", game, sizeof(game));
    info_value(info, "mapname", map, sizeof(map));
    info_value(info, "gametype", gametype, sizeof(gametype));
    info_value(info, "workshop_id", workshop, sizeof(workshop));
    info_value(info, "usermapId", usermap, sizeof(usermap));
    info_value(info, "hostname", host, sizeof(host));
    t7_log("[BO3MacFix] BOIII: %s '%s' map %s %s protocol %s/%s\n", text, host, map, gametype, protocol, sub);
    if (strcmp(protocol, "8") || (strcmp(sub, "1") && strcmp(sub, "0")) || strcmp(game, "T7") || !*map || !*gametype) {
        t7_log("[BO3MacFix] BOIII: unsupported server (protocol/gamename)\n"); free(text); return NULL;
    }
    if (*workshop || *usermap) {
        t7_log("[BO3MacFix] BOIII: server needs workshop content, not supported yet\n"); free(text); return NULL;
    }
    memcpy(target.ip, ip, 4);
    target.port = port;
    snprintf(target.map, sizeof(target.map), "%s", map);
    snprintf(target.gametype, sizeof(target.gametype), "%s", gametype);
    snprintf(target.printable, sizeof(target.printable), "%s", text);
    have_challenge = 0;
    target.launch = 1; // picked up by boiii_frame on the main thread
    free(text);
    return NULL;
}

static void start_connect(const char *text) {
    if (!enabled()) { t7_log("[BO3MacFix] BOIII: set BO3MACFIX_BOIII=1 in bo3macfix.conf first\n"); return; }
    if (!load_or_create_key()) return;
    pthread_t thread;
    char *copy = strdup(text);
    if (copy && !pthread_create(&thread, NULL, connect_thread, copy)) pthread_detach(thread);
    else free(copy);
}

static void boiii_connect_command(void) {
    uintptr_t (*get_tls)(void) = (void *)MAC_ADDR(0xd0604f);
    const uint8_t *args = *(const uint8_t **)(get_tls() + 0x18);
    int nesting = *(const int32_t *)args;
    int argc = *(const int32_t *)(args + 0x64 + nesting * 4);
    const char **argv = *(const char ***)(args + 0x88 + nesting * 8);
    if (argc < 2) { t7_log("[BO3MacFix] BOIII: usage boiii_connect ip:port\n"); return; }
    start_connect(argv[1]);
}

static uint64_t auto_connect_at;
static char auto_connect_target[128];

// Main thread, once per frame.
void boiii_frame(void) {
    if (auto_connect_at && now_ms() >= auto_connect_at) {
        auto_connect_at = 0;
        start_connect(auto_connect_target);
    }
    if (target.launch) {
        target.launch = 0;
        target.active = 1;
        target.started_ms = now_ms();
        last_challenge_request = 0;
        const char *error = NULL;
        t7_log("[BO3MacFix] BOIII: launching client for %s %s\n", target.map, target.gametype);
        if (!CL_LaunchClient(target.map, target.gametype, &error)) {
            t7_log("[BO3MacFix] BOIII: CL_LaunchClient failed: %s\n", error ? error : "(no reason)");
            target.active = 0;
        }
        return;
    }
    if (target.active) {
        int32_t state = *client_state(0);
        if (state > CONNSTATE_CONNECTING) {
            t7_log("[BO3MacFix] BOIII: connected to %s (state %d)\n", target.printable, state);
            target.active = 0;
        } else if (now_ms() - target.started_ms > 30000) {
            t7_log("[BO3MacFix] BOIII: connection timed out (state %d)\n", state);
            target.active = 0;
        }
    }
}

void boiii_client_install(void) {
    if (!t7_known_mac_build()) return;
    NET_OutOfBandPrint = t7_resolve_function("NET_OutOfBandPrint");
    CL_GetNetSrc = t7_resolve_function("CL_GetNetSrc");
    CL_LaunchClient = t7_resolve_function("CL_LaunchClient");
    void *resend = t7_resolve_function("CL_CheckForResend"), *oob = t7_resolve_function("NET_OutOfBandData");
    if (!NET_OutOfBandPrint || !CL_GetNetSrc || !CL_LaunchClient || !resend || !oob) {
        t7_log("[BO3MacFix] BOIII: client functions not mapped\n"); return;
    }
    if (!enabled()) return;
    int a = DobbyHook(resend, (void *)hook_CL_CheckForResend, (void **)&orig_CL_CheckForResend);
    int b = DobbyHook(oob, (void *)hook_NET_OutOfBandData, (void **)&orig_NET_OutOfBandData);
    t7_log("[BO3MacFix] BOIII: client hooks %s\n", !a && !b ? "active" : "failed");
}

void boiii_client_register(void) {
    if (!t7_known_mac_build()) return;
    static uint8_t command_storage[0x40];
    void (*add_command)(const char *, void (*)(void), void *) = (void *)MAC_ADDR(0xf7651d);
    add_command("boiii_connect", boiii_connect_command, command_storage);
    const char *automatic = getenv("BO3MACFIX_BOIII_CONNECT");
    if (enabled() && automatic && *automatic) {
        // Give the game time to reach the main menu before launching.
        snprintf(auto_connect_target, sizeof(auto_connect_target), "%s", automatic);
        auto_connect_at = now_ms() + 20000;
        t7_log("[BO3MacFix] BOIII: auto-connect to %s in 20 s\n", auto_connect_target);
    }
}
