#ifndef PROBE_CORE_H
#define PROBE_CORE_H
#include "efi.h"
/* Probe limits. Exceeding a limit is reported as `unsupported` (a probe
 * limitation), never as `malformed` firmware data. */
#define MAP_LIMIT (256u * 1024u)        /* largest pool buffer requested for the map */
#define MAP_INITIAL (4096u)
#define MAP_ATTEMPTS 5
#define EFI_PAGE_BYTES UINT64_C(4096)
#define DT_LIMIT (1024u * 1024u)        /* largest declared FDT totalsize parsed */
#define DT_DEPTH_LIMIT 64
#define DT_MODEL_LIMIT 64               /* reported model bytes, excluding NUL */
#define VENDOR_LIMIT 64                 /* reported FirmwareVendor CHAR16 units */
#define CONFIG_TABLE_LIMIT 256          /* configuration-table entries searched */
#define REPORTED_DESCRIPTORS 12
#ifndef RECORD_CAPACITY
#define RECORD_CAPACITY 4096           /* bytes; tests may shrink it */
#endif

/* Status policy (see README):
 *   observed    - firmware supplied the value and it passed validation;
 *   truncated   - valid value, only a prefix fits the local output limit;
 *   unavailable - firmware did not supply it (absent/missing service/error);
 *   unsupported - supplied in a version/representation/size outside the
 *                 probe's contract or limits;
 *   malformed   - supplied but internally inconsistent or invalid;
 *   not_probed  - not examined (dependency failed or search limit reached). */
typedef enum { OBSERVED, UNAVAILABLE, UNSUPPORTED, MALFORMED, NOT_PROBED, TRUNCATED } State;

typedef struct { State state; const char *detail; uint32_t count, stride, version;
    struct { uint32_t type; uint64_t physical, pages, attributes; } entries[REPORTED_DESCRIPTORS]; } MapResult;
typedef struct { State state; const char *detail; uint32_t size, version, last_comp_version;
    State model_state; uint32_t model_length; char model[DT_MODEL_LIMIT + 1]; } DtResult;
typedef struct { State state; const char *detail; EFI_STATUS status; uint8_t value; } VarResult;
typedef struct { State state; uint64_t raw; unsigned level; } ElResult;
typedef struct { State state; unsigned units; CHAR16 text[VENDOR_LIMIT + 1]; } VendorResult;
typedef struct { char *data; size_t length, capacity; int truncated; } Writer;

int range_ok(uint64_t start, uint64_t length);
/* capacity: bytes actually allocated and passed to GetMemoryMap;
 * length/stride/version: values returned by GetMemoryMap. Reads only
 * [data, data + length) and only after length <= capacity is checked. */
MapResult parse_map(const void *data, size_t capacity, size_t length, size_t stride, uint32_t version);
/* Classify a BUFFER_TOO_SMALL response; on success *next is the new capacity. */
MapResult map_next_capacity(size_t capacity, size_t required, size_t stride, size_t *next);
/* Reads only [data, data + available). */
DtResult parse_dt(const uint8_t *data, size_t available);
/* Firmware-owned blob: reads 8 header bytes, then at most its declared
 * totalsize (<= DT_LIMIT). See the trust-boundary note in core.c. */
DtResult parse_firmware_dt(const uint8_t *data);
VarResult parse_var(EFI_STATUS status, size_t size, uint8_t value);
ElResult decode_el(uint64_t raw);
/* Reads at most VENDOR_LIMIT + 1 units, stopping at the first NUL. */
VendorResult read_vendor(const CHAR16 *text);
void put(Writer *w, const char *text);
/* JSON string body for NUL-terminated ASCII bytes (0x01..0x7f). */
void escaped(Writer *w, const char *text);
/* JSON string body for UCS-2 units; callers reject surrogates first. */
void escaped16(Writer *w, const CHAR16 *text, unsigned units);
const char *state_name(State s);
#endif
