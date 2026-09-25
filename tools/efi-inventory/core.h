#ifndef PROBE_CORE_H
#define PROBE_CORE_H
#include "efi.h"
#define MAP_LIMIT (256u * 1024u)
#define MAP_ATTEMPTS 5
#define DT_LIMIT (1024u * 1024u)
typedef enum { OBSERVED, UNAVAILABLE, UNSUPPORTED, MALFORMED, NOT_PROBED } State;
#define REPORTED_DESCRIPTORS 12
typedef struct { State state; uint32_t count, stride, version;
    struct { uint32_t type; uint64_t physical, pages, attributes; } entries[REPORTED_DESCRIPTORS]; } MapResult;
typedef struct { State state; uint32_t size, version; char model[65]; } DtResult;
typedef struct { State state; uint8_t value; } VarResult;
typedef struct { State state; uint64_t raw; unsigned level; } ElResult;
typedef struct { char *data; size_t length, capacity; int truncated; } Writer;
int range_ok(uint64_t start, uint64_t length);
MapResult parse_map(const void *data, size_t length, size_t stride, uint32_t version);
DtResult parse_dt(const uint8_t *data, size_t available);
VarResult parse_var(EFI_STATUS status, size_t size, uint8_t value);
ElResult decode_el(uint64_t raw);
void put(Writer *w, const char *text);
void escaped(Writer *w, const char *text);
const char *state_name(State s);
#endif
