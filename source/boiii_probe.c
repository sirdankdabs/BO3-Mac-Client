// Phase 1 of connecting to BOIII/Ezz community servers (see docs/BOIII-MAC-PORT.md):
// a read-only probe. It asks the server for its info and a connect challenge
// on a private UDP socket and logs every reply to t7-status.log. It does not
// join the server and does not touch the game's own socket or identity.
//
// Triggers: console command "boiii_probe ip:port", or BO3MACFIX_BOIII_PROBE=ip:port
// in bo3macfix.conf (runs once, shortly after startup).
#include "t7_runtime.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <mach-o/dyld.h>

#define MAC_ADDR(rva) ((uintptr_t)(rva) + 0x100000000ull + (uintptr_t)_dyld_get_image_vmaddr_slide(0))

static bool resolve(const char *text, struct sockaddr_in *out) {
    char host[256];
    snprintf(host, sizeof(host), "%s", text);
    unsigned port = 27017;
    char *colon = strrchr(host, ':');
    if (colon) { *colon = 0; port = (unsigned)strtoul(colon + 1, NULL, 10); }
    if (!*host || !port || port > 65535) return false;
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &result) || !result) return false;
    *out = *(struct sockaddr_in *)result->ai_addr;
    out->sin_port = htons((uint16_t)port);
    freeaddrinfo(result);
    return true;
}

// Sends one out-of-band packet and waits up to 3 s for a reply.
static ssize_t exchange(int sock, const struct sockaddr_in *to, const char *payload, size_t length,
                        uint8_t *reply, size_t capacity) {
    uint8_t packet[1100];
    if (length + 4 > sizeof(packet)) return -1;
    memset(packet, 0xff, 4);
    memcpy(packet + 4, payload, length);
    if (sendto(sock, packet, length + 4, 0, (const struct sockaddr *)to, sizeof(*to)) < 0) return -1;
    return recvfrom(sock, reply, capacity, 0, NULL, NULL);
}

static void log_info_string(const char *info) {
    // "\key\value\key\value..." -> one line per pair
    char copy[2048];
    snprintf(copy, sizeof(copy), "%s", info);
    char *cursor = copy + (copy[0] == '\\');
    while (cursor && *cursor) {
        char *key = cursor, *value = strchr(key, '\\');
        if (!value) break;
        *value++ = 0;
        char *next = strchr(value, '\\');
        if (next) *next++ = 0;
        t7_log("[BO3MacFix] BOIII probe:   %s = %s\n", key, value);
        cursor = next;
    }
}

static void *probe_thread(void *argument) {
    char *target = argument;
    struct sockaddr_in address;
    if (!resolve(target, &address)) {
        t7_log("[BO3MacFix] BOIII probe: cannot resolve '%s' (use ip:port)\n", target);
        free(target); return NULL;
    }
    char printable[64];
    snprintf(printable, sizeof(printable), "%s:%u", inet_ntoa(address.sin_addr), ntohs(address.sin_port));
    t7_log("[BO3MacFix] BOIII probe: querying %s\n", printable);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    uint8_t reply[4096];

    char query[64];
    int query_length = snprintf(query, sizeof(query), "getInfo\n%u", (unsigned)arc4random());
    ssize_t received = exchange(sock, &address, query, (size_t)query_length, reply, sizeof(reply) - 1);
    if (received <= 4) {
        t7_log("[BO3MacFix] BOIII probe: no infoResponse from %s (server down or blocked)\n", printable);
    } else {
        reply[received] = 0;
        char *body = strchr((char *)reply + 4, '\n');
        t7_log("[BO3MacFix] BOIII probe: infoResponse (%zd bytes)\n", received);
        if (body) log_info_string(body + 1);
    }

    received = exchange(sock, &address, "getchallenge", 12, reply, sizeof(reply));
    const char prefix[] = "getChallengeResponse ";
    if (received >= (ssize_t)(4 + sizeof(prefix) - 1 + 32) && !memcmp(reply + 4, prefix, sizeof(prefix) - 1)) {
        char hex[65];
        for (int i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", reply[4 + sizeof(prefix) - 1 + i]);
        t7_log("[BO3MacFix] BOIII probe: getChallengeResponse OK, challenge %s\n", hex);
        t7_log("[BO3MacFix] BOIII probe: server speaks the BOIII handshake; phase 1 OK.\n");
    } else if (received > 4) {
        reply[received < (ssize_t)sizeof(reply) ? received : (ssize_t)sizeof(reply) - 1] = 0;
        t7_log("[BO3MacFix] BOIII probe: unexpected challenge reply: %.60s\n", (char *)reply + 4);
    } else {
        t7_log("[BO3MacFix] BOIII probe: no reply to getchallenge\n");
    }
    close(sock);
    free(target);
    return NULL;
}

void boiii_probe_start(const char *target) {
    if (!target || !*target) return;
    pthread_t thread;
    char *copy = strdup(target);
    if (copy && !pthread_create(&thread, NULL, probe_thread, copy)) pthread_detach(thread);
    else free(copy);
}

// Console command. Arguments live in the game's TLS command block (see
// CL_ConnectionlessCMD notes): nesting at +0, argc at +0x64, argv at +0x88.
static void boiii_probe_command(void) {
    uintptr_t (*get_tls)(void) = (void *)MAC_ADDR(0xd0604f);
    const uint8_t *args = *(const uint8_t **)(get_tls() + 0x18);
    int nesting = *(const int32_t *)args;
    int argc = *(const int32_t *)(args + 0x64 + nesting * 4);
    const char **argv = *(const char ***)(args + 0x88 + nesting * 8);
    if (argc < 2) { t7_log("[BO3MacFix] BOIII probe: usage boiii_probe ip:port\n"); return; }
    boiii_probe_start(argv[1]);
}

// Called once from the main thread after the game has initialized.
void boiii_register(void) {
    if (!t7_known_mac_build()) return;
    static uint8_t command_storage[0x40];
    void (*add_command)(const char *, void (*)(void), void *) = (void *)MAC_ADDR(0xf7651d);
    add_command("boiii_probe", boiii_probe_command, command_storage);
    t7_log("[BO3MacFix] BOIII: console command boiii_probe registered.\n");
    const char *automatic = getenv("BO3MACFIX_BOIII_PROBE");
    if (automatic && *automatic) boiii_probe_start(automatic);
}
