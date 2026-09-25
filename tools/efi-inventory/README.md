# EFI inventory probe (Task 004, Phase A only)

Standalone development tool; not a Muen component. No board execution is authorized.
The C sources `efi.h`, `core.[ch]`, `probe.c`, and the single `CurrentEL`
instruction in `current_el.S` produce an AArch64 EFI application without an
EFI startup library or third-party DT parser. Only bounded FDT metadata is
parsed, not a general device tree. `tests.c` supplies host-only firmware doubles.

Build inputs: Debian LLVM 19.1.7 (`clang`, `lld-link`, `llvm-readobj`,
`llvm-objdump`), GCC 14.2 and Clang 19.1.7 for host tests, Python 3 for
record decoding. No external source, downloaded EFI library, or linked
runtime is used. Run from this directory:

```sh
make test            # sanitizer host doubles: gcc/clang x -O0/-O2 + small record
make                 # AArch64 EFI image in $(OUT)
make fixtures-check DTC=/path/to/dtc   # regenerate fixtures.h and compare
/usr/lib/llvm-19/bin/llvm-readobj --file-headers --sections --coff-imports --coff-basereloc /tmp/muen-efi-inventory-build/inventory.efi
sha256sum /tmp/muen-efi-inventory-build/inventory.efi
```

`OUT` (default `/tmp/muen-efi-inventory-build`) and `TEST_OUT` hold all
objects, dependency files, test binaries and captured records outside the
tree; nothing is installed. Objects are built with `-MMD -MP`, so changing
`efi.h` or `core.h` rebuilds `core.obj` and `probe.obj` and relinks. The
image links only `core.obj`, `probe.obj` and `current_el.obj` with
`/nodefaultlib`; `core.c` supplies `memcpy`/`memset` for compiler-generated
copies. The 4 KiB report buffer is static (writable, non-executable `.data`)
so that no `__chkstk` stack probe is needed.

## ABI declarations

`efi.h` declares the system table, boot/runtime service tables, text output
protocol and loaded-image protocol in full field order, reviewed against
TianoCore EDK2 commit `2f1971dd4d770c418f9aadb60b18c5645af3344c`
(`MdePkg/Include/Uefi/UefiSpec.h`, `UefiMultiPhase.h`, `UefiBaseType.h`,
`Base.h`, `Protocol/LoadedImage.h`, `Protocol/SimpleTextOut.h`,
`Guid/GlobalVariable.h`, `EmbeddedPkg/Include/Guid/Fdt.h`). This removed a
spurious padding member after `FirmwareRevision` (natural alignment already
places `ConsoleInHandle` at offset 40). `_Static_assert`s pin the
size/offset of every field used; they compile in the AArch64 target build
and every host build. They check this header only. Host doubles do not
exercise the AArch64 firmware calling convention and are **not** evidence
of firmware ABI compatibility.

## Ownership and trust boundary

Every firmware pointer is borrowed and never freed. The only owned memory
is a successful `AllocatePool` map buffer, released exactly once with
`FreePool` on every path (tested for success, retry, rejection, allocation
failure and release failure). A failed allocation transfers no ownership.
A failed `FreePool` is reported in `release_status`, is not retried, and
stops further map attempts. `HandleProtocol` returns a borrowed pointer.

C code cannot check that firmware pointers are readable. The probe reads at
most 65 CHAR16 units of `FirmwareVendor`, 256 configuration-table entries,
and for the FDT the 8-byte magic/`totalsize` prefix and then at most the
declared `totalsize` (at most 1 MiB). `totalsize` is a declared length, not
proof that memory is readable. No exception handler, MMIO or memory scan is
used to work around a bad pointer.

## Memory map

Up to 5 `AllocatePool(EfiLoaderData)` attempts of at most 256 KiB, starting
at 4 KiB and growing to the reported size plus two descriptors after
`EFI_BUFFER_TOO_SMALL`. Before any descriptor is read, the returned size is
checked against the capacity actually allocated and passed to
`GetMemoryMap` (`size_exceeds_buffer`). Stride must be at least 40 bytes
(extended strides accepted), size a non-zero multiple of stride, version 1.
Each descriptor is checked for page-count overflow, physical/virtual range
overflow (a range ending exactly at 2^64 is rejected) and 4 KiB physical
alignment. At most 12 descriptors are listed; `omitted` counts the rest.

## Device tree contract

Only the configuration table with `FDT_TABLE_GUID` is examined. Supported:
FDT `version >= 17` with `last_comp_version <= 17`, i.e. blobs a v17 reader
may interpret. v16 and earlier are `unsupported` (`version`) because they
lack `size_dt_struct`; no v17-only field is read before the version check.
Validation: magic; `totalsize` within the span and 1 MiB; reservation map
8-byte aligned; structure block 4-byte aligned in offset and size; all
blocks inside `totalsize` and pairwise disjoint; reservation map terminated
by a zero entry before the next block, with non-overflowing entries; exactly
one unnamed root node; balanced nesting of at most 64 levels; `FDT_NOP`
anywhere; no property outside the root or after a root subnode; property
lengths/names in bounds and NUL-terminated; `FDT_END` exactly at the end of
the structure block. Anything else is `malformed` with a `detail` string.
The parser reads only `[blob, blob + span)`.

Blob validity is separate from the report limit: the root `model` has its
own status. A valid printable-ASCII model longer than 64 bytes is
`truncated` (first 64 bytes plus the true `length`) inside an `observed`
tree; a string list or non-ASCII model is `unsupported`; an empty,
unterminated or duplicated model is `malformed`; no model is `unavailable`.

Valid fixtures in `fixtures.h` are generated from DTS sources by
`gen_fixtures.py` with an external `dtc` (DTC 1.6.1 from the local bob
`devel/dtc` build); `make fixtures-check` regenerates and compares them.
The leading-NOP fixture is derived from a dtc blob and checked with
`fdtdump`, because the dtc 1.6.1 blob reader rejects a leading NOP although
the specification says NOPs are ignored. Invalid cases are targeted
mutations of those blobs or of a small builder that is itself checked
byte-for-byte against the dtc minimal tree.

## Output and return status

ConOut prints a human banner line and one JSON object, then CRLF. The
record is printable ASCII (every other code unit becomes a `\uXXXX`
escape) and is bounded by a 4096-byte buffer (largest tested record: 2699
bytes). Output is sent in chunks of at most 126 CHAR16 units.

| Condition | Return |
|---|---|
| no system table | `EFI_INVALID_PARAMETER` |
| no ConOut/OutputString (nothing observed or emitted) | `EFI_UNSUPPORTED` |
| an OutputString call returns an error | that status; output stops at once |
| record exceeds the buffer; `{"schema":2,"record_truncated":true}` emitted | `EFI_BUFFER_TOO_SMALL` |
| full record emitted | `EFI_SUCCESS` |

OutputString warnings (for example `EFI_WARN_UNKNOWN_GLYPH`) mean the text
was displayed and are tolerated. Failed observations are values inside a
successfully emitted report, not error returns.

## Record schema 2

Every observation is an object with `status` and `detail` (string or
`null`); values not observed are `null`. Hex values are 16-digit
`0x`-prefixed strings; `efi_status` fields carry raw EFI status codes.

| Status | Meaning |
|---|---|
| `observed` | supplied by firmware and validated |
| `truncated` | valid, but only a prefix fits the local limit |
| `unavailable` | not supplied (absent, missing service, EFI error) |
| `unsupported` | outside the probe's version/representation/size contract |
| `malformed` | supplied but inconsistent or invalid |
| `not_probed` | not examined (dependency failed or search limit reached) |

- `firmware_vendor`: CHAR16 read as UCS-2; up to 64 units, each
  non-printable-ASCII unit `\uXXXX`-escaped; `truncated` when longer;
  surrogate code units give `unsupported`/`ucs2_surrogate`, never an empty
  string.
- `firmware_dt`: reports `tables_searched`/`tables_total`. When more than
  256 entries exist and none of the first 256 matches, the status is
  `not_probed`/`search_limit`, not `unavailable`.
- `SecureBoot`/`SetupMode`: one-byte `GetVariable`, called in its own
  statement before its outputs are read. Success with size 1 and value 0/1
  is `observed`; another size or `EFI_BUFFER_TOO_SMALL` is
  `malformed`/`variable_size`; value > 1 is `malformed`/`value_range`;
  `EFI_NOT_FOUND` is `unavailable`/`not_found`; `EFI_UNSUPPORTED` is
  `unsupported`; other errors are `unavailable`/`efi_error`; warnings are
  `unsupported`.
- `boot_services_map`: `efi_status`, `attempts`, `release_status`, `count`,
  `descriptor_stride`, `descriptor_version`, `descriptors`, `omitted`.
  Details: `service_missing`, `allocation_failed`, `allocation_null`,
  `efi_error`, `warning_status`, `size_exceeds_buffer`,
  `descriptor_version`, `stride`, `empty`, `partial_descriptor`,
  `pages_overflow`, `range_overflow`, `unaligned_physical`,
  `too_small_stride`, `too_small_inconsistent`, `size_limit`,
  `retry_limit`, `release_failed`.
- `loaded_image`, `device_path` (pointer presence only; never
  dereferenced), `firmware_revision`, `uefi_revision`, and `CurrentEL`
  (`raw`, `reported_level`).

The map is a **boot-services snapshot**, not a final ExitBootServices map.
The DT model is a bounded root property, not proof of a Linux DT or
physical ownership. A reported EL2 is not independent evidence of physical
EL2 or non-secure state. Policy bytes are not fuse evidence or
image-signature/enforcement proof. Missing DT is normal on generic firmware.

## Host tests

`tests.c` runs 94 named cases (plus one small-record build) with
AddressSanitizer and UndefinedBehaviorSanitizer under GCC and Clang at
`-O0` and `-O2`. Each case resets the doubles, fills every unused
boot/runtime/console service slot with a forbidden-call sentinel, runs
`efi_main` or a parser, and asserts exact JSON fragments. After every case
the harness checks zero sentinel calls and exactly-once release of every
allocation. Firmware doubles allocate exact-size heap buffers so any
over-read is a sanitizer finding. `check_records.py` then decodes each
captured record with a strict JSON parser and checks key values.

The application does not call ExitBootServices, SetVirtualAddressMap,
SetVariable, ResetSystem, PSCI, capsules, key enrollment, device MMIO,
additional system registers, or timer/watchdog modification. It does not
install vectors/stacks, manipulate DAIF/MMU/cache/GIC/SMMU, load payloads,
start CPUs/devices, write disks or firmware, or persist a record. Firmware
services can themselves operate hardware; no DMA-quiescence claim is made.

## Future hardware gate (not completed)

Record actual module SKU/RAM, carrier, firmware/BSP, console route, Secure
Boot policy and image authorization independently; obtain owner approval and
a reviewed non-destructive one-shot launch, output-capture, normal-boot and
recovery procedure. Confirm accepted image signing without changing keys or
fuses. Do not copy this image to board media, reboot, flash, or execute on Orin
under Task 004. Final map, carveouts, security context, GIC/SMMU ownership,
DMA state, and all Phase B prerequisites remain unknown.
