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
// Mac connstate values, from CL_CheckForResend's jump table (0xe1861c):
// 4 sends "getchallenge", 5 builds and sends "connect", 6 sends "cfl".
#define CONNSTATE_CHALLENGING 4
#define CONNSTATE_CONNECTING 5

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
    volatile int routing;     // packets to/from the server bypass Demonware
    uint8_t ip[4];
    uint16_t port;
    char map[64], gametype[64], printable[64];
    uint64_t started_ms;
} target;
static uint8_t challenge[32];
static volatile int have_challenge;
// OX's BOIII server issues a new challenge on every "getchallenge" and only
// accepts a signature over the latest one, so each connect uses a challenge
// requested just before it, once.
static volatile int challenge_used;
static uint64_t last_challenge_request;

// Controller XUIDs live at *(*(0x20915f0 + 8*controller)) (read by
// LiveUser_GetControllerFromXuid, 0x103a79e). BOIII servers address us by the
// key-derived XUID, so controller 0 uses it while a BOIII session is active and
// gets its Steam XUID back afterwards.
static uint64_t saved_xuid;
static uint64_t steam_xuid;
static int xuid_swapped;
static void use_boiii_xuid(bool enable) {
    uint64_t *slot = *(uint64_t **)MAC_ADDR(0x20915f0);
    if (!slot || !t7_memory_writable(slot, sizeof(*slot))) return;
    if (enable && !xuid_swapped) {
        saved_xuid = *slot;
        *slot = key_xuid;
        xuid_swapped = 1;
        t7_log("[BO3MacFix] BOIII: controller 0 xuid %llx -> %llx\n", (unsigned long long)saved_xuid,
               (unsigned long long)key_xuid);
    } else if (!enable && xuid_swapped) {
        if (*slot == key_xuid) *slot = saved_xuid;
        xuid_swapped = 0;
        t7_log("[BO3MacFix] BOIII: controller 0 xuid restored\n");
    }
}

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
static bool (*Com_IsLocalServerRunning)(void);
static void (*Com_KillLocalServerForLaunch)(void);

static void (*orig_CL_CheckForResend)(int);
static void hook_CL_CheckForResend(int client) {
    if (target.active && client == 0) {
        uint8_t *address = client_server_address(0);
        int32_t *state = client_state(0);
        if (address && *state >= CONNSTATE_CHALLENGING && *state <= CONNSTATE_CONNECTING + 1) {
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
    if (challenge_used) {
        // A resend: fetch a fresh challenge first (CL_CheckForResend hook), the
        // game sends connect again once it arrives.
        have_challenge = 0;
        last_challenge_request = 0;
        return true;
    }
    challenge_used = 1;

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

static volatile int signature_retries;
static void accept_challenge(const uint8_t *bytes) {
    memcpy(challenge, bytes, sizeof(challenge));
    challenge_used = 0;
    have_challenge = 1;
    int32_t *state = client_state(0);
    if (*state == CONNSTATE_CHALLENGING) *state = CONNSTATE_CONNECTING;
    t7_log("[BO3MacFix] BOIII: challenge %02x%02x%02x%02x... received, connecting\n",
           challenge[0], challenge[1], challenge[2], challenge[3]);
}

// BOIII servers answer some out-of-band commands with raw "\xff\xff\xff\xff..."
// packets (no socket byte, no checksum) that the game itself would drop.
static void handle_raw_packet(const uint8_t *data, size_t length) {
    static const char prefix[] = "getChallengeResponse ";
    const size_t offset = 4 + sizeof(prefix) - 1;
    if (length >= offset + sizeof(challenge) && !memcmp(data + 4, prefix, sizeof(prefix) - 1)) {
        if (target.active) accept_challenge(data + offset);
        return;
    }
    char text[96];
    size_t n = length - 4 < sizeof(text) - 1 ? length - 4 : sizeof(text) - 1;
    for (size_t i = 0; i < n; ++i) text[i] = (data[4 + i] >= 0x20 && data[4 + i] < 0x7f) ? (char)data[4 + i] : '.';
    text[n] = 0;
    if (strncmp(text, "profileInfo", 11) && strncmp(text, "playerXuid", 10))
        t7_log("[BO3MacFix] BOIII: server says: %s\n", text);
    // The server verifies against the challenge it holds for our address; if
    // that no longer matches ours, ask for a fresh one and connect again.
    if (!strncmp(text, "error Bad signature", 19) && target.active && signature_retries < 3) {
        ++signature_retries;
        have_challenge = 0;
        last_challenge_request = 0;
        t7_log("[BO3MacFix] BOIII: requesting a fresh challenge (retry %d)\n", signature_retries);
    }
}

bool boiii_session_active(void) { return target.routing != 0; }

// Called from the T7 connectionless filter before it decides. Returns true when
// the packet was consumed here.
bool boiii_connectionless(const char *command, const void *msg) {
    if (!target.routing || !command) return false;
    if (!strcmp(command, "getChallengeResponse") || !strcmp(command, "profileInfo") || !strcmp(command, "playerXuid"))
        return true; // raw-only BOIII commands, handled in the socket layer
    if (!strcmp(command, "error") || !strcmp(command, "connectResponse") || !strcmp(command, "print"))
        t7_log("[BO3MacFix] BOIII: server sent '%s'\n", command);
    return false;
}

// --------------------------------------------------------------- networking --
// BOIII's wire format (client and server): one socket byte + payload, no
// checksum, sent with plain sendto on the IP socket; its own replies may come
// raw. Mac sends IP packets through Demonware (dwSendTo), which refuses
// addresses it has no connection for, so traffic with the BOIII server is
// carried on the game's UDP socket directly and fed back through Sys_GetPacket.
static volatile int game_socket = -1;
// Traffic counters for diagnostics (logged every 5 s while routing).
static volatile unsigned stat_sent, stat_sent_bytes, stat_recv, stat_recv_bytes, stat_raw, stat_injected, stat_dropped;
static volatile unsigned logged_sent;
static void hex_prefix(const uint8_t *data, size_t length, char *out, size_t capacity);
// Logs each distinct out-of-band command (first word) once per direction.
static void log_oob_once(const char *direction, const uint8_t *data, size_t length) {
    static char seen[64][24];
    static unsigned count;
    char word[24];
    size_t n = 0;
    while (n < length && n < sizeof(word) - 1 && data[n] > 0x20 && data[n] < 0x7f) { word[n] = (char)data[n]; ++n; }
    word[n] = 0;
    char key[24];
    snprintf(key, sizeof(key), "%c%s", direction[0], word);
    for (unsigned i = 0; i < count; ++i) if (!strcmp(seen[i], key)) return;
    if (count < 64) snprintf(seen[count++], sizeof(seen[0]), "%s", key);
    t7_log("[BO3MacFix] BOIII: %s OOB '%s' (%zu bytes)\n", direction, word, length);
}
static void log_type_once(const uint8_t *packet, size_t length) {
    static uint8_t seen[256];
    if (length < 2 || seen[packet[1]]) return;
    seen[packet[1]] = 1;
    char hex[80];
    hex_prefix(packet, length < 28 ? length : 28, hex, sizeof(hex));
    t7_log("[BO3MacFix] BOIII: server packet type %02x, %zu bytes: %s\n", packet[1], length, hex);
}
static void hex_prefix(const uint8_t *data, size_t length, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; i < length && used + 3 < capacity; ++i) used += (size_t)snprintf(out + used, capacity - used, "%02x", data[i]);
    out[used] = 0;
}
static volatile uint64_t server_xuid;

#define QUEUE_SIZE 256
static struct { uint8_t data[1600]; uint16_t length; } packet_queue[QUEUE_SIZE];
static unsigned queue_head, queue_tail;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;

static bool from_server(const struct sockaddr *from) {
    if (!from || from->sa_family != AF_INET) return false;
    const struct sockaddr_in *in = (const struct sockaddr_in *)from;
    return !memcmp(&in->sin_addr, target.ip, 4) && ntohs(in->sin_port) == target.port;
}

static ssize_t recvfrom_hook(int fd, void *buffer, size_t capacity, int flags, struct sockaddr *from, socklen_t *from_length) {
    for (;;) {
        ssize_t received = recvfrom(fd, buffer, capacity, flags, from, from_length);
        if (received <= 0 || !target.routing || fd != game_socket || !from_server(from)) return received;
        const uint8_t *bytes = buffer;
        ++stat_recv;
        stat_recv_bytes += (unsigned)received;
        if (received >= 4 && !memcmp(bytes, "\xff\xff\xff\xff", 4)) {
            ++stat_raw;
            handle_raw_packet(bytes, (size_t)received);
        } else if ((size_t)received <= sizeof(packet_queue[0].data)) {
            if (received >= 6 && !memcmp(bytes + 1, "\xff\xff\xff\xff", 4)) log_oob_once("server", bytes + 5, (size_t)received - 5);
            else log_type_once(bytes, (size_t)received);
            static unsigned dumped_server;
            if (*client_state(0) >= 10 && dumped_server < 16) {
                ++dumped_server;
                char hex[400];
                hex_prefix(bytes, received < 190 ? (size_t)received : 190, hex, sizeof(hex));
                t7_log("[BO3MacFix] BOIII: S> %zd: %s\n", received, hex);
            }
            // Netchan packets: [socket byte][type][u64 dest xuid][u64 from xuid]...
            if (!server_xuid && received >= 18) {
                uint64_t dest, from_xuid;
                memcpy(&dest, bytes + 2, 8);
                memcpy(&from_xuid, bytes + 10, 8);
                if (dest == key_xuid && from_xuid) {
                    server_xuid = from_xuid;
                    t7_log("[BO3MacFix] BOIII: server xuid %llx\n", (unsigned long long)server_xuid);
                }
            }
            pthread_mutex_lock(&queue_lock);
            if (queue_tail - queue_head < QUEUE_SIZE) {
                memcpy(packet_queue[queue_tail % QUEUE_SIZE].data, bytes, (size_t)received);
                packet_queue[queue_tail % QUEUE_SIZE].length = (uint16_t)received;
                ++queue_tail;
            } else ++stat_dropped;
            pthread_mutex_unlock(&queue_lock);
        } else {
            ++stat_dropped;
        }
        // Hidden from Demonware; read the next datagram (the socket is non-blocking).
    }
}
DYLD_INTERPOSE(recvfrom_hook, recvfrom);

// Sys_GetPacket(netadr_t *from, msg_t *msg): hand queued server packets to the
// game exactly as it would after stripping the socket byte, minus the checksum.
static bool (*orig_Sys_GetPacket)(uint8_t *, uint8_t *);
static bool hook_Sys_GetPacket(uint8_t *from, uint8_t *msg) {
    if (target.routing) {
        uint8_t packet[1600];
        uint16_t length = 0;
        pthread_mutex_lock(&queue_lock);
        if (queue_head != queue_tail) {
            length = packet_queue[queue_head % QUEUE_SIZE].length;
            memcpy(packet, packet_queue[queue_head % QUEUE_SIZE].data, length);
            ++queue_head;
        }
        pthread_mutex_unlock(&queue_lock);
        int32_t capacity;
        memcpy(&capacity, msg + 0x18, 4);
        uint8_t *data = *(uint8_t **)(msg + 8);
        if (length >= 2 && data && length <= capacity) {
            uint8_t socket_byte = packet[0];
            if ((socket_byte & 0xf) < 5 && (socket_byte >> 4) < 5) {
                memset(from, 0, 16);
                memcpy(from, target.ip, 4);
                memcpy(from + 4, &target.port, 2);
                // Same end state as the original: socket byte read (readcount 1),
                // data pointer advanced past it, cursize = payload length.
                int32_t type = 4, local_id = socket_byte & 0xf, remote = socket_byte >> 4, one = 1, size = length - 1;
                memcpy(from + 8, &type, 4);
                memcpy(from + 0xc, &local_id, 4);
                memcpy(data, packet, length);
                memcpy(msg + 0x34, &remote, 4);
                memcpy(msg + 0x24, &one, 4);  // readcount
                uint8_t *advanced = data + 1;
                memcpy(msg + 8, &advanced, sizeof(advanced));
                memcpy(msg + 0x1c, &size, 4); // cursize
                ++stat_injected;
                return true;
            }
        }
    }
    return orig_Sys_GetPacket(from, msg);
}

// While connecting to / playing on a BOIII server, the lobby system's
// "return to frontend" (restart core_frontend and reconnect to it) would pull
// the client off the server. Defer it until the BOIII session is over.
static void (*orig_Com_ReturnToFrontend)(void);
static volatile int frontend_deferred;
static void use_boiii_xuid(bool enable);
static int32_t *client_state(int client);
static void hook_Com_ReturnToFrontend(void) {
    if (target.routing && *client_state(0) >= CONNSTATE_CHALLENGING) {
        // Still connecting/connected to the BOIII server: this is the menu's
        // lobby reacting to its killed local server. Hold it.
        if (!frontend_deferred) t7_log("[BO3MacFix] BOIII: deferring return to frontend\n");
        frontend_deferred = 1;
        return;
    }
    if (target.routing) {
        // The client already dropped (error or disconnect): end the BOIII
        // session and let the game return to the menu right away.
        t7_log("[BO3MacFix] BOIII: session ended, returning to frontend\n");
        target.routing = 0;
        target.active = 0;
        use_boiii_xuid(false);
    }
    frontend_deferred = 0;
    orig_Com_ReturnToFrontend();
}

// The game keeps the Steam XUID (its stats are bound to it); packets the BOIII
// server addresses to our key XUID belong to controller 0.
static int (*orig_LiveUser_GetControllerFromXuid)(uint64_t);
static int hook_LiveUser_GetControllerFromXuid(uint64_t xuid) {
    if (target.routing && xuid == key_xuid) return 0;
    return orig_LiveUser_GetControllerFromXuid(xuid);
}

// Server id. The Mac client reads it from config string 3, which a BOIII server
// (final Windows build) leaves empty, so the client reports 0 and the server
// keeps resending the gamestate (the intro loops).
//
// What the server checks (Mac SV_ExecuteClientMessage, 0x105bd66):
//   id == sv.serverId          -> normal processing
//   (id ^ sv.serverId) & 0xf0 == 0 -> enters the world if PRIMED, else the
//                                 packet is ignored (map_restart in flight)
//   otherwise                  -> gamestate resent
// sv.serverId = (old + 0x10) & 0xff per map load, high nibble never 0; the low
// nibble only moves on map_restart.
//
// Search: every gamestate after the first one means the server rejected the
// id we sent (or loaded a new map, which also adds 0x10), so step the high
// nibble on each new gamestate and keep it while none arrives. The client can
// take a while to go active after the server lets it in, so no timers here.
// Once in game, fix the low nibble by watching whether the server
// acknowledges our reliable commands. The last working id per server is kept
// in boiii_serverids.txt as the next starting guess.
static struct {
    int fixed, active, value, high, low, low_tries, gamestates;
    uint64_t pending_since;
} sid = {0, 0, 0x10, 1, 0, 0, 0, 0};

static void set_server_id(int value) {
    uintptr_t cl = *(uintptr_t *)MAC_ADDR(0x11534598);
    if (cl && *(int32_t *)MAC_ADDR(0x11534590) > 0) *(int32_t *)(cl + 0xb684) = value;
    *(int32_t *)MAC_ADDR(0x115345b0 + 0x2bf0c8) = value;
}

static int saved_server_id(bool store, int value) {
    char path[1024], line[160];
    file_next_to_dylib("boiii_serverids.txt", path, sizeof(path));
    char lines[32][160];
    int count = 0, found = 0;
    FILE *file = fopen(path, "r");
    if (file) {
        while (count < 32 && fgets(line, sizeof(line), file)) {
            char name[128]; unsigned id;
            if (sscanf(line, "%127s %x", name, &id) != 2) continue;
            if (!strcmp(name, target.printable)) { found = (int)id; if (store) continue; }
            snprintf(lines[count++], sizeof(lines[0]), "%s %02x\n", name, id & 0xff);
        }
        fclose(file);
    }
    if (!store) return found;
    if ((file = fopen(path, "w"))) {
        fprintf(file, "%s %02x\n", target.printable, value & 0xff);
        for (int i = 0; i < count && i < 31; ++i) fputs(lines[i], file);
        fclose(file);
    }
    return value;
}

static void server_id_reset(void) {
    const char *fixed = getenv("BO3MACFIX_BOIII_SERVERID");
    int value = fixed && *fixed ? (int)strtol(fixed, NULL, 0) & 0xff : 0;
    sid.fixed = value >= 0x10;
    if (!sid.fixed) value = saved_server_id(false, 0);
    if (value < 0x10) value = 0x10;
    sid.active = 0;
    sid.value = value;
    sid.high = value >> 4;
    sid.low = value & 0xf;
    sid.low_tries = 0;
    sid.gamestates = 0;
    sid.pending_since = 0;
    t7_log("[BO3MacFix] BOIII: serverId starts at 0x%02x%s\n", value, sid.fixed ? " (fixed in bo3macfix.conf)" : "");
}

// Called for every gamestate the client parses.
static void server_id_gamestate(void) {
    if (++sid.gamestates > 1 && !sid.fixed) {
        sid.high = sid.high % 15 + 1;
        sid.value = (sid.high << 4) | sid.low;
        t7_log("[BO3MacFix] BOIII: gamestate #%d, server rejected the id; trying 0x%02x\n", sid.gamestates, sid.value);
    }
    sid.active = 0;
    set_server_id(sid.value);
}

// Main thread, every frame while routing.
static void server_id_frame(void) {
    int32_t state = *client_state(0);
    if (state < 9) return;
    set_server_id(sid.value);
    if (state <= 10) return;
    uint64_t now = now_ms();
    if (!sid.active) {
        sid.active = 1;
        sid.pending_since = 0;
        t7_log("[BO3MacFix] BOIII: in game (state %d), serverId 0x%02x\n", state, sid.value);
        if (!sid.fixed) saved_server_id(true, sid.value);
    }
    if (sid.fixed) return;
    // Low nibble: with only the high nibble right the server ignores our
    // packets, so our reliable commands (clc+0x130 sent, +0x134 acked) stay
    // unacknowledged.
    uintptr_t clc = *(uintptr_t *)MAC_ADDR(0x115345a0);
    if (!clc) return;
    int32_t sent = *(int32_t *)(clc + 0x130), acked = *(int32_t *)(clc + 0x134);
    if (sent <= acked) { sid.pending_since = 0; return; }
    if (!sid.pending_since) { sid.pending_since = now; return; }
    if (now - sid.pending_since > 3000 && sid.low_tries < 16) {
        ++sid.low_tries;
        sid.low = (sid.low + 1) & 0xf;
        sid.value = (sid.high << 4) | sid.low;
        sid.pending_since = now;
        t7_log("[BO3MacFix] BOIII: commands not acknowledged (%d/%d), trying serverId 0x%02x\n", acked, sent,
               sid.value);
        saved_server_id(true, sid.value);
    }
}

// CL_ParseServerId runs from CL_ParseGamestate (0xecaa64..0xecb04d) and when
// config string 3 changes.
static void (*orig_CL_ParseServerId)(int);
static void hook_CL_ParseServerId(int client) {
    orig_CL_ParseServerId(client);
    if (!target.routing || client != 0) return;
    const char *(*get_configstring)(int) = t7_resolve_function("CL_GetConfigString");
    const char *direct = get_configstring ? get_configstring(3) : NULL;
    if (direct && *direct) return;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (caller >= MAC_ADDR(0xecaa64) && caller < MAC_ADDR(0xecb04d)) server_id_gamestate();
    else set_server_id(sid.value);
}

// Sys_SendPacket(netsrc, length, data, netadr_t to): plain sendto for the server.
static bool (*orig_Sys_SendPacket)(int, int, const uint8_t *, uint64_t, uint64_t);
static bool hook_Sys_SendPacket(int socket_id, int length, const uint8_t *data, uint64_t lo, uint64_t hi) {
    if (target.routing && game_socket >= 0 && (uint32_t)hi == 4 && is_target(lo) && length >= 0 && length < 1600) {
        uint8_t packet[1601];
        packet[0] = (uint8_t)((socket_id & 0xf) | (((int)(hi >> 32) & 0xf) << 4));
        memcpy(packet + 1, data, (size_t)length);
        // Netchan packets: [type][u64 dest][u64 from]... -> present our key XUID.
        if (length >= 17 && memcmp(data, "\xff\xff\xff\xff", 4)) {
            uint64_t from_xuid;
            memcpy(&from_xuid, packet + 10, 8);
            if (steam_xuid && from_xuid == steam_xuid) memcpy(packet + 10, &key_xuid, 8);
        }
        ++stat_sent;
        stat_sent_bytes += (unsigned)length + 1;
        if (length >= 5 && !memcmp(data, "\xff\xff\xff\xff", 4)) log_oob_once("client", data + 4, (size_t)length - 4);
        static unsigned dumped_client;
        if (*client_state(0) >= 10 && dumped_client < 10) {
            ++dumped_client;
            char hex[400];
            hex_prefix(packet, length + 1 < 190 ? (size_t)length + 1 : 190, hex, sizeof(hex));
            t7_log("[BO3MacFix] BOIII: C> %d: %s\n", length + 1, hex);
        }
        if (logged_sent < 6 && length >= 4 && memcmp(data, "\xff\xff\xff\xff", 4)) {
            ++logged_sent;
            char hex[80];
            hex_prefix(packet, length + 1 < 24 ? (size_t)length + 1 : 24, hex, sizeof(hex));
            t7_log("[BO3MacFix] BOIII: client packet %d bytes: %s\n", length + 1, hex);
        }
        struct sockaddr_in to = {0};
        to.sin_family = AF_INET;
        to.sin_port = htons(target.port);
        memcpy(&to.sin_addr, target.ip, 4);
        return sendto(game_socket, packet, (size_t)length + 1, 0, (struct sockaddr *)&to, sizeof(to)) >= 0;
    }
    return orig_Sys_SendPacket(socket_id, length, data, lo, hi);
}

// Demonware binds the game's UDP socket on net_port (27017, or the next free
// port); remember it so BOIII traffic can use it. Some builds bind it to
// 127.0.0.1 (net_ip "localhost"); internet servers need all interfaces.
static int bind_hook(int socket_fd, const struct sockaddr *address, socklen_t length) {
    if (enabled() && address && address->sa_family == AF_INET && length >= (socklen_t)sizeof(struct sockaddr_in)) {
        struct sockaddr_in copy;
        memcpy(&copy, address, sizeof(copy));
        int type = 0;
        socklen_t type_length = sizeof(type);
        const unsigned port = ntohs(copy.sin_port);
        if (port >= 27017 && port <= 27027 &&
            !getsockopt(socket_fd, SOL_SOCKET, SO_TYPE, &type, &type_length) && type == SOCK_DGRAM) {
            if (copy.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) copy.sin_addr.s_addr = htonl(INADDR_ANY);
            int result = bind(socket_fd, (const struct sockaddr *)&copy, sizeof(copy));
            if (!result) game_socket = socket_fd;
            return result;
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

// Queries one server; on success arms the launch. Returns true when picked.
static bool try_server(const char *text) {
    uint8_t ip[4]; uint16_t port;
    if (!resolve(text, ip, &port)) {
        t7_log("[BO3MacFix] BOIII: cannot resolve '%s'\n", text); return false;
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
    if (received <= 4) { t7_log("[BO3MacFix] BOIII: %s did not answer getInfo\n", text); return false; }
    reply[received] = 0;
    const char *info = strchr(reply + 4, '\n');
    info = info ? info + 1 : "";
    char clients[16], max_clients[16], protocol[16], sub[16], game[16], map[64], gametype[64], workshop[64], usermap[64], host[128];
    info_value(info, "protocol", protocol, sizeof(protocol));
    info_value(info, "sub_protocol", sub, sizeof(sub));
    info_value(info, "gamename", game, sizeof(game));
    info_value(info, "mapname", map, sizeof(map));
    info_value(info, "gametype", gametype, sizeof(gametype));
    info_value(info, "workshop_id", workshop, sizeof(workshop));
    info_value(info, "usermapId", usermap, sizeof(usermap));
    info_value(info, "hostname", host, sizeof(host));
    info_value(info, "clients", clients, sizeof(clients));
    info_value(info, "sv_maxclients", max_clients, sizeof(max_clients));
    t7_log("[BO3MacFix] BOIII: %s '%s' map %s %s protocol %s/%s\n", text, host, map, gametype, protocol, sub);
    if (strcmp(protocol, "8") || (strcmp(sub, "1") && strcmp(sub, "0")) || strcmp(game, "T7") || !*map || !*gametype) {
        t7_log("[BO3MacFix] BOIII: unsupported server (protocol/gamename)\n"); return false;
    }
    if (*workshop || *usermap) {
        t7_log("[BO3MacFix] BOIII: server needs workshop content, not supported yet\n"); return false;
    }
    if (*max_clients && atoi(clients) >= atoi(max_clients)) {
        t7_log("[BO3MacFix] BOIII: %s is full (%s/%s)\n", text, clients, max_clients); return false;
    }
    memcpy(target.ip, ip, 4);
    target.port = port;
    snprintf(target.map, sizeof(target.map), "%s", map);
    snprintf(target.gametype, sizeof(target.gametype), "%s", gametype);
    snprintf(target.printable, sizeof(target.printable), "%s", text);
    have_challenge = 0;
    target.launch = 1; // picked up by boiii_frame on the main thread
    return true;
}

// Accepts one server or a comma-separated list; joins the first usable one.
static void *connect_thread(void *argument) {
    char *list = argument, *cursor = list, *item;
    bool picked = false;
    while (!picked && (item = strsep(&cursor, ", ")))
        if (*item) picked = try_server(item);
    if (!picked) t7_log("[BO3MacFix] BOIII: no usable server (all full or unsupported)\n");
    free(list);
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
    // Auto-connect once the player has been sitting in the Zombies menu (not
    // in a match) for 5 seconds.
    if (auto_connect_at) {
        // The menus run a local "core_frontend" server the client is connected
        // to, so only the mode is checked here.
        if (!bo3_is_zombies()) auto_connect_at = now_ms() + 5000;
        else if (now_ms() >= auto_connect_at) {
            auto_connect_at = 0;
            t7_log("[BO3MacFix] BOIII: Zombies menu detected, auto-connecting\n");
            start_connect(auto_connect_target);
        }
    }
    if (target.launch) {
        target.launch = 0;
        target.active = 1;
        target.routing = 1;
        signature_retries = 0;
        server_xuid = 0;
        server_id_reset();
        stat_sent = stat_sent_bytes = stat_recv = stat_recv_bytes = stat_raw = stat_injected = stat_dropped = 0;
        logged_sent = 0;
        pthread_mutex_lock(&queue_lock);
        queue_head = queue_tail = 0;
        pthread_mutex_unlock(&queue_lock);
        t7_log("[BO3MacFix] BOIII: game socket fd %d\n", game_socket);
        target.started_ms = now_ms();
        last_challenge_request = 0;
        const char *error = NULL;
        // CL_Connect skips the network handshake when cls.servername is already
        // "localhost" and the client is connected (the menu's local server).
        // Give it another name so it takes the challenge/connect path.
        snprintf((char *)MAC_ADDR(0x115345b4), 0x100, "%s", "boiii");
        // Same order as the lobby's client launch (LaunchGameGo, 0x10885f6):
        // shut the menu's local server down first so its level memory is freed.
        if (Com_IsLocalServerRunning()) {
            t7_log("[BO3MacFix] BOIII: stopping the menu's local server\n");
            Com_KillLocalServerForLaunch();
        }
        uint64_t *slot = *(uint64_t **)MAC_ADDR(0x20915f0);
        steam_xuid = slot ? *slot : 0;
        t7_log("[BO3MacFix] BOIII: launching client for %s %s (state %d)\n", target.map, target.gametype,
               *client_state(0));
        if (!CL_LaunchClient(target.map, target.gametype, &error)) {
            t7_log("[BO3MacFix] BOIII: CL_LaunchClient failed: %s\n", error ? error : "(no reason)");
            target.active = 0;
            target.routing = 0;
        }
        return;
    }
    // Diagnostics: dump the first config strings once the gamestate is parsed.
    static int dumped_configstrings;
    if (target.routing && !dumped_configstrings && *client_state(0) >= 10) {
        const char *(*get_configstring)(int) = t7_resolve_function("CL_GetConfigString");
        if (get_configstring) {
            dumped_configstrings = 1;
            int shown = 0;
            for (int i = 3; i < 0xe2e && shown < 60; ++i) {
                const char *value = get_configstring(i);
                if (value && *value && strlen(value) <= 24) {
                    ++shown;
                    t7_log("[BO3MacFix] BOIII: cs %d = %s\n", i, value);
                }
            }
        }
    }
    if (target.routing) server_id_frame();
    static uint64_t next_stats;
    if (target.routing && now_ms() >= next_stats) {
        if (next_stats)
            t7_log("[BO3MacFix] BOIII: serverId 0x%02x%s state %d sent %u/%uB recv %u/%uB raw %u injected %u dropped %u\n",
                   sid.value, sid.active ? " (in game)" : "", *client_state(0), stat_sent, stat_sent_bytes, stat_recv, stat_recv_bytes, stat_raw, stat_injected,
                   stat_dropped);
        next_stats = now_ms() + 5000;
    }
    // CL_WritePacket sends to the netchan peer at clc+0x24b68 (xuid) /
    // clc+0x2455c (address). The menu's lobby made us our own "host", so point
    // both at the BOIII server once we are connected.
    if (target.routing && server_xuid && *client_state(0) > CONNSTATE_CONNECTING) {
        uint8_t *clc = client_server_address(0);
        if (clc) {
            clc -= 0x10;
            uint64_t *peer_xuid = (uint64_t *)(clc + 0x24b68);
            uint8_t *peer_address = clc + 0x2455c;
            int32_t ip_type = 4;
            if (*peer_xuid != server_xuid || memcmp(peer_address, target.ip, 4) || memcmp(peer_address + 8, &ip_type, 4)) {
                int32_t old_type;
                memcpy(&old_type, peer_address + 8, 4);
                t7_log("[BO3MacFix] BOIII: netchan peer %llx (type %d) -> %llx %s\n", (unsigned long long)*peer_xuid,
                       old_type, (unsigned long long)server_xuid, target.printable);
                *peer_xuid = server_xuid;
                memcpy(peer_address, target.ip, 4);
                memcpy(peer_address + 4, &target.port, 2);
                memcpy(peer_address + 8, &ip_type, 4);
            }
        }
    }
    if (target.active) {
        int32_t state = *client_state(0);
        if (state > CONNSTATE_CONNECTING) {
            t7_log("[BO3MacFix] BOIII: connected to %s (state %d)\n", target.printable, state);
            target.active = 0;
        } else if (now_ms() - target.started_ms > 30000) {
            t7_log("[BO3MacFix] BOIII: connection timed out (state %d)\n", state);
            target.active = 0;
            target.routing = 0;
        }
    } else if (target.routing && *client_state(0) < CONNSTATE_CHALLENGING) {
        t7_log("[BO3MacFix] BOIII: left %s\n", target.printable);
        target.routing = 0;
    }
    if (!target.routing && xuid_swapped) use_boiii_xuid(false);
    if (!target.routing && frontend_deferred) {
        frontend_deferred = 0;
        t7_log("[BO3MacFix] BOIII: returning to frontend\n");
        orig_Com_ReturnToFrontend();
    }
}

void boiii_client_install(void) {
    if (!t7_known_mac_build()) return;
    NET_OutOfBandPrint = t7_resolve_function("NET_OutOfBandPrint");
    CL_GetNetSrc = t7_resolve_function("CL_GetNetSrc");
    CL_LaunchClient = t7_resolve_function("CL_LaunchClient");
    Com_IsLocalServerRunning = t7_resolve_function("Com_IsLocalServerRunning");
    Com_KillLocalServerForLaunch = t7_resolve_function("Com_KillLocalServerForLaunch");
    void *resend = t7_resolve_function("CL_CheckForResend"), *oob = t7_resolve_function("NET_OutOfBandData");
    if (!NET_OutOfBandPrint || !CL_GetNetSrc || !CL_LaunchClient || !Com_IsLocalServerRunning ||
        !Com_KillLocalServerForLaunch || !resend || !oob) {
        t7_log("[BO3MacFix] BOIII: client functions not mapped\n"); return;
    }
    if (!enabled()) return;
    void *get_packet = t7_resolve_function("Sys_GetPacket"), *send_packet = t7_resolve_function("Sys_SendPacket");
    void *frontend = t7_resolve_function("Com_ReturnToFrontend");
    if (!get_packet || !send_packet || !frontend) { t7_log("[BO3MacFix] BOIII: packet functions not mapped\n"); return; }
    int a = DobbyHook(resend, (void *)hook_CL_CheckForResend, (void **)&orig_CL_CheckForResend);
    int b = DobbyHook(oob, (void *)hook_NET_OutOfBandData, (void **)&orig_NET_OutOfBandData);
    int c = DobbyHook(get_packet, (void *)hook_Sys_GetPacket, (void **)&orig_Sys_GetPacket);
    int d = DobbyHook(send_packet, (void *)hook_Sys_SendPacket, (void **)&orig_Sys_SendPacket);
    int e = DobbyHook(frontend, (void *)hook_Com_ReturnToFrontend, (void **)&orig_Com_ReturnToFrontend);
    void *server_id = t7_resolve_function("CL_ParseServerId");
    if (server_id) DobbyHook(server_id, (void *)hook_CL_ParseServerId, (void **)&orig_CL_ParseServerId);
    void *lookup = t7_resolve_function("LiveUser_GetControllerFromXuid");
    int f = lookup ? DobbyHook(lookup, (void *)hook_LiveUser_GetControllerFromXuid,
                               (void **)&orig_LiveUser_GetControllerFromXuid) : -1;
    t7_log("[BO3MacFix] BOIII: client hooks %s\n", !a && !b && !c && !d && !e && !f ? "active" : "failed");
}

void boiii_client_register(void) {
    if (!t7_known_mac_build()) return;
    static uint8_t command_storage[0x40];
    void (*add_command)(const char *, void (*)(void), void *) = (void *)MAC_ADDR(0xf7651d);
    add_command("boiii_connect", boiii_connect_command, command_storage);
    const char *automatic = getenv("BO3MACFIX_BOIII_CONNECT");
    if (enabled() && automatic && *automatic) {
        // Fires from boiii_frame once the Zombies menu is reached.
        snprintf(auto_connect_target, sizeof(auto_connect_target), "%s", automatic);
        auto_connect_at = now_ms() + 5000;
        t7_log("[BO3MacFix] BOIII: will auto-connect to %s from the Zombies menu\n", auto_connect_target);
    }
}
