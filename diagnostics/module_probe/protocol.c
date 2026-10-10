#include "probe.h"
#include <string.h>

/* Exactly six hexadecimal digits, aligned to one 256-byte read-only block. */
int probe_flash_address(const char *text, uint32_t *address)
{
    uint32_t value = 0;
    if (strlen(text) != 6u) return 0;
    for (unsigned i = 0; i < 6u; ++i) {
        unsigned digit;
        char c = text[i];
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else return 0;
        value = value * 16u + digit;
    }
    if (value > 0x07FF00u || (value & 255u) != 0u) return 0;
    *address = value;
    return 1;
}
