/* Minimal UEFI 2.x AArch64 ABI declarations used by this application only.
 * No startup library or firmware-owned object is linked into the image. */
#ifndef PROBE_EFI_H
#define PROBE_EFI_H
#include <stdint.h>
#include <stddef.h>
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } EFI_GUID;
#define EFI_ERROR(x) ((x) & (UINT64_C(1)<<63))
#define EFI_BUFFER_TOO_SMALL (UINT64_C(1)<<63 | 5)
#define EFI_NOT_FOUND (UINT64_C(1)<<63 | 14)
#define EFI_SUCCESS 0
#define EFI_LOADER_DATA 2
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 2
typedef struct { uint32_t type; uint32_t pad; uint64_t physical, virtual_address, pages, attributes; } EFI_MEMORY_DESCRIPTOR;
typedef struct { EFI_GUID guid; void *table; } EFI_CONFIGURATION_TABLE;
/* EFI_TABLE_HEADER is 24 bytes; firmware vendor follows immediately. */
typedef struct { uint64_t signature; uint32_t revision, header_size, crc32, reserved; } EFI_TABLE_HEADER;
typedef struct { EFI_TABLE_HEADER hdr; CHAR16 *firmware_vendor; uint32_t firmware_revision;
    uint32_t padding; EFI_HANDLE console_in_handle; void *con_in; EFI_HANDLE console_out_handle;
    struct EFI_SIMPLE_TEXT_OUTPUT *con_out; EFI_HANDLE console_err_handle; void *std_err;
    void *runtime_services; struct EFI_BOOT_SERVICES *boot_services;
    size_t number_of_table_entries; EFI_CONFIGURATION_TABLE *configuration_table; } EFI_SYSTEM_TABLE;
typedef struct EFI_SIMPLE_TEXT_OUTPUT { void *reset; EFI_STATUS (*output_string)(struct EFI_SIMPLE_TEXT_OUTPUT *, const CHAR16 *); } EFI_SIMPLE_TEXT_OUTPUT;
typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl, *restore_tpl;
    EFI_STATUS (*allocate_pages)(void); EFI_STATUS (*free_pages)(void);
    EFI_STATUS (*get_memory_map)(size_t *, EFI_MEMORY_DESCRIPTOR *, size_t *, size_t *, uint32_t *);
    EFI_STATUS (*allocate_pool)(uint32_t, size_t, void **);
    EFI_STATUS (*free_pool)(void *);
    void *create_event, *set_timer, *wait_for_event, *signal_event, *close_event, *check_event;
    void *install_protocol_interface, *reinstall_protocol_interface, *uninstall_protocol_interface;
    EFI_STATUS (*handle_protocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *reserved, *register_protocol_notify, *locate_handle, *locate_device_path;
    void *install_configuration_table, *load_image, *start_image, *exit, *unload_image;
    void *exit_boot_services, *get_next_monotonic_count, *stall, *set_watchdog_timer;
    void *connect_controller, *disconnect_controller, *open_protocol, *close_protocol;
} EFI_BOOT_SERVICES;
typedef struct { uint32_t revision; EFI_HANDLE parent_handle; EFI_SYSTEM_TABLE *system_table;
    EFI_HANDLE device_handle; void *file_path; void *reserved; uint32_t load_options_size;
    void *load_options; void *image_base; uint64_t image_size; uint32_t image_code_type;
    uint32_t image_data_type; EFI_STATUS (*unload)(EFI_HANDLE); } EFI_LOADED_IMAGE;
typedef struct { EFI_TABLE_HEADER hdr; EFI_STATUS (*get_time)(void); void *set_time, *get_wakeup_time, *set_wakeup_time;
    void *set_virtual_address_map, *convert_pointer;
    EFI_STATUS (*get_variable)(CHAR16 *, EFI_GUID *, uint32_t *, size_t *, void *); } EFI_RUNTIME_SERVICES_PREFIX;
static const EFI_GUID loaded_image_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID dt_guid = {0xb1b621d5,0xf19c,0x41a5,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
static const EFI_GUID global_guid = {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
#endif
