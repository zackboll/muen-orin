/* Minimal UEFI 2.x AArch64 ABI declarations used by this application only.
 * No startup library or firmware-owned object is linked into the image.
 *
 * Reviewed against TianoCore EDK2 commit
 * 2f1971dd4d770c418f9aadb60b18c5645af3344c: MdePkg/Include/Uefi/UefiSpec.h,
 * Uefi/UefiMultiPhase.h, Uefi/UefiBaseType.h, Base.h, Protocol/LoadedImage.h,
 * Protocol/SimpleTextOut.h, Guid/Fdt.h (EmbeddedPkg) and
 * Guid/GlobalVariable.h. Service tables are declared in full field order so
 * host tests can fill every unused slot with a forbidden-call sentinel; slots
 * the probe never calls are opaque pointers.
 *
 * The static assertions check the used fields for any 64-bit data model
 * (target LLP64 and host LP64); `make` and `make test` both compile them.
 * They do not prove that any particular firmware implements the spec. */
#ifndef PROBE_EFI_H
#define PROBE_EFI_H
#include <stdint.h>
#include <stddef.h>
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } EFI_GUID;
#define EFI_ERROR_BIT (UINT64_C(1)<<63)
#define EFI_ERROR(x) (((x) & EFI_ERROR_BIT) != 0)
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER (EFI_ERROR_BIT | 2)
#define EFI_UNSUPPORTED (EFI_ERROR_BIT | 3)
#define EFI_BAD_BUFFER_SIZE (EFI_ERROR_BIT | 4)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_BIT | 5)
#define EFI_DEVICE_ERROR (EFI_ERROR_BIT | 7)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_BIT | 9)
#define EFI_NOT_FOUND (EFI_ERROR_BIT | 14)
#define EFI_SECURITY_VIOLATION (EFI_ERROR_BIT | 26)
#define EFI_WARN_UNKNOWN_GLYPH UINT64_C(1)
#define EFI_LOADER_DATA 2
#define EFI_MEMORY_DESCRIPTOR_VERSION 1
typedef struct { uint32_t type; uint32_t pad; uint64_t physical, virtual_address, pages, attributes; } EFI_MEMORY_DESCRIPTOR;
typedef struct { EFI_GUID guid; void *table; } EFI_CONFIGURATION_TABLE;
typedef struct { uint64_t signature; uint32_t revision, header_size, crc32, reserved; } EFI_TABLE_HEADER;
typedef struct { EFI_TABLE_HEADER hdr; CHAR16 *firmware_vendor; uint32_t firmware_revision;
    EFI_HANDLE console_in_handle; void *con_in; EFI_HANDLE console_out_handle;
    struct EFI_SIMPLE_TEXT_OUTPUT *con_out; EFI_HANDLE console_err_handle; void *std_err;
    struct EFI_RUNTIME_SERVICES *runtime_services; struct EFI_BOOT_SERVICES *boot_services;
    size_t number_of_table_entries; EFI_CONFIGURATION_TABLE *configuration_table; } EFI_SYSTEM_TABLE;
typedef struct EFI_SIMPLE_TEXT_OUTPUT {
    void *reset;
    EFI_STATUS (*output_string)(struct EFI_SIMPLE_TEXT_OUTPUT *, const CHAR16 *);
    void *test_string, *query_mode, *set_mode, *set_attribute, *clear_screen;
    void *set_cursor_position, *enable_cursor, *mode;
} EFI_SIMPLE_TEXT_OUTPUT;
typedef struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl, *restore_tpl;
    void *allocate_pages, *free_pages;
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
    void *open_protocol_information, *protocols_per_handle, *locate_handle_buffer;
    void *locate_protocol, *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces, *calculate_crc32, *copy_mem, *set_mem;
    void *create_event_ex;
} EFI_BOOT_SERVICES;
typedef struct { uint32_t revision; EFI_HANDLE parent_handle; EFI_SYSTEM_TABLE *system_table;
    EFI_HANDLE device_handle; void *file_path; void *reserved; uint32_t load_options_size;
    void *load_options; void *image_base; uint64_t image_size; uint32_t image_code_type;
    uint32_t image_data_type; void *unload; } EFI_LOADED_IMAGE;
typedef struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER hdr;
    void *get_time, *set_time, *get_wakeup_time, *set_wakeup_time;
    void *set_virtual_address_map, *convert_pointer;
    EFI_STATUS (*get_variable)(CHAR16 *, EFI_GUID *, uint32_t *, size_t *, void *);
    void *get_next_variable_name, *set_variable, *get_next_high_monotonic_count, *reset_system;
    void *update_capsule, *query_capsule_capabilities, *query_variable_info;
} EFI_RUNTIME_SERVICES;

#define PROBE_ABI(t, f, o) _Static_assert(offsetof(t, f) == (o), #t "." #f " offset")
_Static_assert(sizeof(void *) == 8 && sizeof(size_t) == 8, "64-bit UEFI data model required");
_Static_assert(sizeof(CHAR16) == 2 && sizeof(EFI_GUID) == 16, "CHAR16/EFI_GUID size");
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24, "EFI_TABLE_HEADER size");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40, "EFI_MEMORY_DESCRIPTOR size");
PROBE_ABI(EFI_MEMORY_DESCRIPTOR, physical, 8); PROBE_ABI(EFI_MEMORY_DESCRIPTOR, virtual_address, 16);
PROBE_ABI(EFI_MEMORY_DESCRIPTOR, pages, 24); PROBE_ABI(EFI_MEMORY_DESCRIPTOR, attributes, 32);
_Static_assert(sizeof(EFI_CONFIGURATION_TABLE) == 24, "EFI_CONFIGURATION_TABLE size");
PROBE_ABI(EFI_CONFIGURATION_TABLE, table, 16);
PROBE_ABI(EFI_TABLE_HEADER, revision, 8);
PROBE_ABI(EFI_SYSTEM_TABLE, firmware_vendor, 24); PROBE_ABI(EFI_SYSTEM_TABLE, firmware_revision, 32);
PROBE_ABI(EFI_SYSTEM_TABLE, con_out, 64); PROBE_ABI(EFI_SYSTEM_TABLE, runtime_services, 88);
PROBE_ABI(EFI_SYSTEM_TABLE, boot_services, 96); PROBE_ABI(EFI_SYSTEM_TABLE, number_of_table_entries, 104);
PROBE_ABI(EFI_SYSTEM_TABLE, configuration_table, 112);
_Static_assert(sizeof(EFI_SYSTEM_TABLE) == 120, "EFI_SYSTEM_TABLE size");
PROBE_ABI(EFI_SIMPLE_TEXT_OUTPUT, output_string, 8);
_Static_assert(sizeof(EFI_SIMPLE_TEXT_OUTPUT) == 80, "EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL size");
PROBE_ABI(EFI_BOOT_SERVICES, get_memory_map, 56); PROBE_ABI(EFI_BOOT_SERVICES, allocate_pool, 64);
PROBE_ABI(EFI_BOOT_SERVICES, free_pool, 72); PROBE_ABI(EFI_BOOT_SERVICES, handle_protocol, 152);
PROBE_ABI(EFI_BOOT_SERVICES, exit_boot_services, 232); PROBE_ABI(EFI_BOOT_SERVICES, set_watchdog_timer, 256);
PROBE_ABI(EFI_BOOT_SERVICES, create_event_ex, 368);
_Static_assert(sizeof(EFI_BOOT_SERVICES) == 376, "EFI_BOOT_SERVICES size");
PROBE_ABI(EFI_RUNTIME_SERVICES, set_virtual_address_map, 56); PROBE_ABI(EFI_RUNTIME_SERVICES, get_variable, 72);
PROBE_ABI(EFI_RUNTIME_SERVICES, set_variable, 88); PROBE_ABI(EFI_RUNTIME_SERVICES, reset_system, 104);
_Static_assert(sizeof(EFI_RUNTIME_SERVICES) == 136, "EFI_RUNTIME_SERVICES size");
PROBE_ABI(EFI_LOADED_IMAGE, file_path, 32); PROBE_ABI(EFI_LOADED_IMAGE, image_base, 64);
PROBE_ABI(EFI_LOADED_IMAGE, image_size, 72); PROBE_ABI(EFI_LOADED_IMAGE, unload, 88);
#undef PROBE_ABI
static const EFI_GUID loaded_image_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static const EFI_GUID dt_guid = {0xb1b621d5,0xf19c,0x41a5,{0x83,0x0b,0xd9,0x15,0x2c,0x69,0xaa,0xe0}};
static const EFI_GUID global_guid = {0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
#endif
