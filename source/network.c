#include "network.h"
#include <pthread.h>
#include <time.h>

static pthread_mutex_t password_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t current_password, previous_password, changed_at;
static bool has_previous;

uint64_t network_now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

void network_set_password(uint64_t password, uint64_t now_ms) {
    pthread_mutex_lock(&password_lock);
    if (password != current_password) {
        previous_password = current_password;
        current_password = password;
        changed_at = now_ms;
        has_previous = true;
    }
    pthread_mutex_unlock(&password_lock);
}

uint64_t network_get_password(void) {
    pthread_mutex_lock(&password_lock);
    uint64_t password = current_password;
    pthread_mutex_unlock(&password_lock);
    return password;
}

bool network_checksum_matches(uint16_t received, uint16_t plain, uint64_t now_ms) {
    pthread_mutex_lock(&password_lock);
    bool matches = received == (uint16_t)(plain ^ current_password);
    if (!matches && has_previous && now_ms >= changed_at && now_ms - changed_at <= 1500)
        matches = received == (uint16_t)(plain ^ previous_password);
    pthread_mutex_unlock(&password_lock);
    return matches;
}

bool network_has_prefix(uint64_t password) {
    // T7Patch omits BOTH prefix bytes when bits 16..23 are zero.
    return ((password >> 16) & 0xff) != 0;
}

bool network_instant_message_valid(const uint8_t *data, int length) {
    // The existing Mac dispatcher hook receives raw bytes, not Windows msg_t.
    return data && length >= 2 && data[0] == 0x31 &&
        data[1] != 0x65 && data[1] != 0x6d;
}
