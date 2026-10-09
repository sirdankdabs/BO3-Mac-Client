#include "network.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint16_t plain = 0x5678;
    assert(network_get_password() == 0);
    assert(network_checksum_matches(plain, plain, 0));
    assert(!network_checksum_matches(plain ^ 1, plain, 0));
    network_set_password(0x12345678, 2000);
    assert(network_get_password() == 0x12345678);
    assert(network_checksum_matches(plain ^ 0x5678, plain, 999999));
    assert(network_checksum_matches(plain, plain, 3500));
    assert(!network_checksum_matches(plain, plain, 3501));
    assert(!network_checksum_matches(plain, plain, 1999));
    // Reapplying the same password must not reopen the grace period.
    network_set_password(0x12345678, 4000);
    assert(!network_checksum_matches(plain, plain, 4000));
    network_set_password(0x42, 5000);
    assert(network_checksum_matches(plain ^ 0x5678, plain, 6500));
    assert(!network_checksum_matches(plain ^ 0x5678, plain, 6501));
    assert(!network_checksum_matches(plain, plain, 5000));
    network_set_password(0, 7000);
    assert(network_checksum_matches(plain ^ 0x42, plain, 8500));
    assert(!network_checksum_matches(plain ^ 0x42, plain, 8501));
    assert(network_checksum_matches(plain, plain, 9000));
    assert(!network_has_prefix(0));
    assert(!network_has_prefix(0xff000001));
    assert(network_has_prefix(0x10000));
    assert(network_has_prefix(0xff0000));
    const uint8_t allowed[] = {0x31, 0x68};
    assert(network_instant_message_valid(allowed, 2));
    assert(!network_instant_message_valid(NULL, 2));
    assert(!network_instant_message_valid(allowed, -1));
    assert(!network_instant_message_valid(allowed, 0));
    assert(!network_instant_message_valid(allowed, 1));
    for (unsigned type = 0; type <= 255; ++type) {
        uint8_t packet[] = {0x31, (uint8_t)type};
        assert(network_instant_message_valid(packet, 2) == (type != 0x65 && type != 0x6d));
        packet[0] = 0;
        assert(!network_instant_message_valid(packet, 2));
    }
    puts("Network regression tests passed.");
}
