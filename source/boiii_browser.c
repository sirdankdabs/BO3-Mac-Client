// Server browser for community (BOIII protocol) servers.
//
// Asks the master server for the T7 server list, queries every server with
// getInfo, keeps the ones whose name matches the filter (BO3MACFIX_BOIII_BROWSER,
// e.g. "OX") and shows them in a native macOS list (osascript "choose from
// list") with map, players and status. Picking one connects to it; the list
// also lets the player change the name used on these servers.
//
// Triggers: opening the Zombies menu (when BO3MACFIX_BOIII_BROWSER is set), or
// the console command "boiii_servers".
#include "t7_runtime.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

void boiii_connect_to(const char *address);
void boiii_file_path(const char *name, char *out, size_t capacity);

#define MASTER_DEFAULT "master.ezz.lol:20810"
#define MAX_SERVERS 256

typedef struct {
    struct sockaddr_in address;
    bool answered;
    char hostname[128], map[48], gametype[32];
    int clients, max_clients, bots;
    bool modded, compatible;
} server_t;

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static bool resolve(const char *text, struct sockaddr_in *out) {
    char host[256];
    snprintf(host, sizeof(host), "%s", text);
    unsigned port = 20810;
    char *colon = strrchr(host, ':');
    if (colon) { *colon = 0; port = (unsigned)strtoul(colon + 1, NULL, 10); }
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (!*host || !port || port > 65535 || getaddrinfo(host, NULL, &hints, &result) || !result) return false;
    *out = *(struct sockaddr_in *)result->ai_addr;
    out->sin_port = htons((uint16_t)port);
    freeaddrinfo(result);
    return true;
}

static void info_value(const char *info, const char *name, char *out, size_t capacity) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\\%s\\", name);
    const char *found = strstr(info, pattern);
    out[0] = 0;
    if (!found) return;
    found += strlen(pattern);
    size_t length = strcspn(found, "\\");
    if (length >= capacity) length = capacity - 1;
    memcpy(out, found, length);
    out[length] = 0;
}

// Removes ^N color codes and characters that would break the AppleScript list.
static void clean_text(const char *in, char *out, size_t capacity) {
    size_t used = 0;
    for (const char *c = in; *c && used + 1 < capacity; ++c) {
        if (*c == '^' && c[1]) { ++c; continue; }
        if (*c == '"' || *c == '\\' || (unsigned char)*c < 0x20) continue;
        out[used++] = *c;
    }
    while (used && (out[used - 1] == ' ' || out[used - 1] == '|')) --used;
    out[used] = 0;
}

static const char *map_name(const char *map) {
    static const char *names[][2] = {
        {"zm_zod", "Shadows of Evil"},       {"zm_factory", "The Giant"},
        {"zm_castle", "Der Eisendrache"},    {"zm_island", "Zetsubou No Shima"},
        {"zm_stalingrad", "Gorod Krovi"},    {"zm_genesis", "Revelations"},
        {"zm_prototype", "Nacht der Untoten"}, {"zm_asylum", "Verruckt"},
        {"zm_sumpf", "Shi No Numa"},         {"zm_theater", "Kino der Toten"},
        {"zm_cosmodrome", "Ascension"},      {"zm_temple", "Shangri-La"},
        {"zm_moon", "Moon"},                 {"zm_tomb", "Origins"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(map, names[i][0])) return names[i][1];
    return map;
}

static bool contains_case(const char *haystack, const char *needle) {
    if (!*needle) return true;
    for (const char *h = haystack; *h; ++h) {
        size_t i = 0;
        while (needle[i] && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) ++i;
        if (!needle[i]) return true;
    }
    return false;
}

// Master query: "getservers\nT7 8 full empty" -> "getserversResponse\n" +
// ("\\" + 4-byte ip + 2-byte port)* + "\\EOT".
static int query_master(server_t *servers, int capacity) {
    const char *master = getenv("BO3MACFIX_BOIII_MASTER");
    struct sockaddr_in address;
    if (!resolve(master && *master ? master : MASTER_DEFAULT, &address)) {
        t7_log("[BO3MacFix] BOIII browser: cannot resolve the master server\n");
        return 0;
    }
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout = {0, 300000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const char query[] = "\xff\xff\xff\xffgetservers\nT7 8 full empty";
    sendto(sock, query, sizeof(query) - 1, 0, (struct sockaddr *)&address, sizeof(address));
    int count = 0;
    uint64_t deadline = now_ms() + 2500;
    bool done = false;
    uint8_t reply[4096];
    while (!done && now_ms() < deadline) {
        ssize_t received = recvfrom(sock, reply, sizeof(reply), 0, NULL, NULL);
        if (received <= 0) { if (count) break; continue; }
        const char header[] = "\xff\xff\xff\xffgetserversResponse";
        if ((size_t)received < sizeof(header) - 1 || memcmp(reply, header, sizeof(header) - 1)) continue;
        for (ssize_t i = sizeof(header) - 1; i < received; ++i) {
            if (reply[i] != '\\') continue;
            if (i + 4 <= received && !memcmp(reply + i + 1, "EOT", 3)) { done = true; break; }
            if (i + 7 > received || count >= capacity) break;
            server_t *server = &servers[count++];
            memset(server, 0, sizeof(*server));
            server->address.sin_family = AF_INET;
            memcpy(&server->address.sin_addr, reply + i + 1, 4);
            memcpy(&server->address.sin_port, reply + i + 5, 2);
            i += 6;
        }
    }
    close(sock);
    return count;
}

static void query_servers(server_t *servers, int count) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct timeval timeout = {0, 200000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const char query[] = "\xff\xff\xff\xffgetInfo\nbo3mac";
    for (int i = 0; i < count; ++i)
        sendto(sock, query, sizeof(query) - 1, 0, (struct sockaddr *)&servers[i].address, sizeof(servers[i].address));
    uint64_t deadline = now_ms() + 1500;
    char reply[4096];
    while (now_ms() < deadline) {
        struct sockaddr_in from;
        socklen_t from_length = sizeof(from);
        ssize_t received = recvfrom(sock, reply, sizeof(reply) - 1, 0, (struct sockaddr *)&from, &from_length);
        if (received <= 4) continue;
        reply[received] = 0;
        if (memcmp(reply, "\xff\xff\xff\xffinfoResponse", 16)) continue;
        const char *info = strchr(reply + 4, '\n');
        if (!info) continue;
        for (int i = 0; i < count; ++i) {
            server_t *server = &servers[i];
            if (server->answered || server->address.sin_addr.s_addr != from.sin_addr.s_addr ||
                server->address.sin_port != from.sin_port)
                continue;
            char value[128], protocol[16], game[16], workshop[64], usermap[64];
            info_value(info, "hostname", value, sizeof(value));
            clean_text(value, server->hostname, sizeof(server->hostname));
            info_value(info, "mapname", server->map, sizeof(server->map));
            info_value(info, "gametype", server->gametype, sizeof(server->gametype));
            info_value(info, "clients", value, sizeof(value));
            server->clients = atoi(value);
            info_value(info, "sv_maxclients", value, sizeof(value));
            server->max_clients = atoi(value);
            info_value(info, "bots", value, sizeof(value));
            server->bots = atoi(value);
            info_value(info, "protocol", protocol, sizeof(protocol));
            info_value(info, "gamename", game, sizeof(game));
            info_value(info, "workshop_id", workshop, sizeof(workshop));
            info_value(info, "usermapId", usermap, sizeof(usermap));
            server->modded = workshop[0] || usermap[0];
            server->compatible = !strcmp(protocol, "8") && !strcmp(game, "T7") && server->map[0];
            server->answered = true;
            break;
        }
    }
    close(sock);
}

// Runs an AppleScript and returns its first output line (empty on cancel).
static bool run_osascript(const char *script, char *out, size_t capacity) {
    int pipe_fds[2];
    out[0] = 0;
    if (pipe(pipe_fds)) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    char *argv[] = {"/usr/bin/osascript", "-e", (char *)script, NULL};
    pid_t pid;
    int spawned = posix_spawn(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_fds[1]);
    if (spawned) { close(pipe_fds[0]); return false; }
    size_t used = 0;
    ssize_t chunk;
    while (used + 1 < capacity && (chunk = read(pipe_fds[0], out + used, capacity - 1 - used)) > 0) used += (size_t)chunk;
    out[used] = 0;
    close(pipe_fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    char *newline = strchr(out, '\n');
    if (newline) *newline = 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && out[0];
}

// Brings the game window back to the front after a dialog.
static void activate_game(void) {
    Class running = objc_getClass("NSRunningApplication");
    if (!running) return;
    id current = ((id (*)(Class, SEL))objc_msgSend)(running, sel_registerName("currentApplication"));
    if (current) ((bool (*)(id, SEL, unsigned long))objc_msgSend)(current, sel_registerName("activateWithOptions:"), 2);
}

// ---- Player name ---------------------------------------------------------

static void sanitize_name(const char *in, char *out, size_t capacity) {
    size_t used = 0;
    for (const char *c = in; *c && used + 1 < capacity && used < 16; ++c) {
        if (*c == '\\' || *c == '"' || *c == ';' || *c == '%' || (unsigned char)*c < 0x20) continue;
        out[used++] = *c;
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = 0;
}

// Replaces (or adds) BO3MACFIX_PLAYERNAME in bo3macfix.conf.
static void save_name(const char *name) {
    char path[1024];
    boiii_file_path("bo3macfix.conf", path, sizeof(path));
    char *contents = NULL;
    size_t size = 0;
    FILE *file = fopen(path, "r");
    if (file) {
        fseek(file, 0, SEEK_END);
        long length = ftell(file);
        fseek(file, 0, SEEK_SET);
        if (length > 0 && length < 1 << 20 && (contents = calloc(1, (size_t)length + 1))) size = fread(contents, 1, (size_t)length, file);
        fclose(file);
    }
    if (!(file = fopen(path, "w"))) { free(contents); return; }
    bool written = false;
    char *line = contents;
    while (line && *line && (size_t)(line - contents) < size) {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (!strncmp(line, "BO3MACFIX_PLAYERNAME=", 21) || !strncmp(line, "#BO3MACFIX_PLAYERNAME=", 22)) {
            if (!written) fprintf(file, "BO3MACFIX_PLAYERNAME=%s\n", name);
            written = true;
        } else {
            fwrite(line, 1, length, file);
            fputc('\n', file);
        }
        line = end ? end + 1 : NULL;
    }
    if (!written) fprintf(file, "\n# Nombre en los servidores de la comunidad (maximo 16)\nBO3MACFIX_PLAYERNAME=%s\n", name);
    fclose(file);
    free(contents);
}

static bool ask_name(void) {
    const char *current = getenv("BO3MACFIX_PLAYERNAME");
    char script[1024], answer[128], safe[64];
    clean_text(current ? current : "", safe, sizeof(safe));
    snprintf(script, sizeof(script),
             "activate\n"
             "set r to display dialog \"Nombre que veran los demas jugadores (maximo 16 caracteres):\" "
             "default answer \"%s\" with title \"BO3MacFix\" buttons {\"Cancelar\", \"Guardar\"} "
             "default button \"Guardar\" cancel button \"Cancelar\"\n"
             "return text returned of r",
             safe);
    if (!run_osascript(script, answer, sizeof(answer))) return false;
    char name[32];
    sanitize_name(answer, name, sizeof(name));
    if (!name[0]) return false;
    setenv("BO3MACFIX_PLAYERNAME", name, 1);
    save_name(name);
    t7_log("[BO3MacFix] BOIII: player name set to '%s'\n", name);
    return true;
}

// ---- Browser ---------------------------------------------------------------

static volatile int browser_running;

static int compare_servers(const void *a, const void *b) {
    // Joinable servers first, then by name.
    const server_t *x = a, *y = b;
    if (x->modded != y->modded) return x->modded ? 1 : -1;
    return strcmp(x->hostname, y->hostname);
}

static void *browser_thread(void *argument) {
    (void)argument;
    const char *filter_env = getenv("BO3MACFIX_BOIII_BROWSER");
    char filter[64];
    snprintf(filter, sizeof(filter), "%s", filter_env && strcmp(filter_env, "1") ? filter_env : "");
    static server_t servers[MAX_SERVERS];
    for (;;) {
        int count = query_master(servers, MAX_SERVERS);
        t7_log("[BO3MacFix] BOIII browser: %d servers from the master\n", count);
        query_servers(servers, count);
        int shown = 0;
        for (int i = 0; i < count; ++i)
            if (servers[i].answered && servers[i].compatible && contains_case(servers[i].hostname, filter))
                servers[shown++] = servers[i];
        qsort(servers, (size_t)shown, sizeof(servers[0]), compare_servers);

        static char script[65536];
        const char *name = getenv("BO3MACFIX_PLAYERNAME");
        char safe_name[64];
        clean_text(name && *name ? name : "el de Steam", safe_name, sizeof(safe_name));
        size_t used = (size_t)snprintf(script, sizeof(script),
                                       "activate\nset l to {\"Cambiar nombre (ahora: %s)\", \"Actualizar lista\"", safe_name);
        for (int i = 0; i < shown && used < sizeof(script) - 512; ++i) {
            server_t *s = &servers[i];
            const char *status = s->modded ? "  - requiere mod (no soportado)"
                               : s->max_clients && s->clients >= s->max_clients ? "  - LLENO" : "";
            used += (size_t)snprintf(script + used, sizeof(script) - used,
                                     ", \"%d. %s  |  %d/%d jugadores  |  %s%s\"", i + 1, map_name(s->map),
                                     s->clients - s->bots, s->max_clients, s->hostname, status);
        }
        snprintf(script + used, sizeof(script) - used,
                 "}\nset r to choose from list l with title \"Servidores%s%s\" with prompt \"Elige un servidor (%d encontrados):\" "
                 "OK button name \"Entrar\" cancel button name \"Cerrar\"\n"
                 "if r is false then return \"\"\nreturn item 1 of r",
                 filter[0] ? " " : "", filter, shown);
        char choice[512];
        bool picked = run_osascript(script, choice, sizeof(choice));
        if (!picked) break;
        if (!strncmp(choice, "Cambiar nombre", 14)) { ask_name(); continue; }
        if (!strncmp(choice, "Actualizar", 10)) continue;
        int index = atoi(choice) - 1;
        if (index < 0 || index >= shown) break;
        server_t *s = &servers[index];
        if (s->modded) {
            run_osascript("activate\ndisplay dialog \"Ese servidor necesita un mod del Workshop; todavia no se "
                          "puede en Mac.\" with title \"BO3MacFix\" buttons {\"OK\"} default button \"OK\"\nreturn \"ok\"",
                          choice, sizeof(choice));
            continue;
        }
        char address[64];
        snprintf(address, sizeof(address), "%s:%u", inet_ntoa(s->address.sin_addr), ntohs(s->address.sin_port));
        t7_log("[BO3MacFix] BOIII browser: joining %s (%s)\n", address, s->hostname);
        activate_game();
        boiii_connect_to(address);
        browser_running = 0;
        return NULL;
    }
    activate_game();
    browser_running = 0;
    return NULL;
}

void boiii_browser_open(void) {
    if (browser_running) return;
    browser_running = 1;
    pthread_t thread;
    if (pthread_create(&thread, NULL, browser_thread, NULL)) browser_running = 0;
    else pthread_detach(thread);
}

bool boiii_browser_enabled(void) {
    const char *value = getenv("BO3MACFIX_BOIII_BROWSER");
    return value && *value && strcmp(value, "0");
}
