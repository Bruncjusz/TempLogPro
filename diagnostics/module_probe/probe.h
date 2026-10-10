#ifndef MODULE_PROBE_H
#define MODULE_PROBE_H
#include <stdint.h>
typedef void (*ProbeEmit)(const char *text);
void probe_command(const char *line, ProbeEmit emit);
int probe_flash_address(const char *text, uint32_t *address);
#endif
