#include "utilities.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// These tests do not modify game memory.
int DobbyCodePatch(void *p, uint8_t *b, uint32_t n) { return 0; }
int main(void) {
    const uint8_t bytes[] = {0, 1, 0x7f, 0x80, 0xff};
    char output[11];
    assert(bytes_to_hex(bytes, sizeof(bytes), output, sizeof(output)));
    assert(strcmp(output, "00017f80ff") == 0);
    assert(!bytes_to_hex(bytes, sizeof(bytes), output, sizeof(output) - 1));
    assert(!bytes_to_hex(bytes, SIZE_MAX, output, sizeof(output)));
    assert(bytes_to_hex(NULL, 0, output, sizeof(output)) && !output[0]);
    assert(!bytes_to_hex(NULL, 1, output, sizeof(output)));
    assert(!bytes_to_hex(bytes, 1, NULL, 3));
    assert(!strcmp(filename_from_path(""), ""));
    assert(!strcmp(filename_from_path(NULL), ""));
    assert(!strcmp(filename_from_path("/test"), "test"));
    assert(!strcmp(filename_from_path("test"), "test"));
    assert(!strcmp(filename_from_path("/dir/test"), "test"));
    assert(gscu_canon_hash64("Password") == gscu_canon_hash64("password"));
    assert(gscu_canon_hash64("password") == UINT64_C(0x4b1a493507b3a318));
    puts("Utility regression tests passed.");
}
