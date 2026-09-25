# EFI inventory probe (Task 004, Phase A only)

Standalone development tool; not a Muen component. No board execution is authorized.
The C sources `efi.h`, `core.[ch]`, `probe.c`, and the single `CurrentEL`
instruction in `current_el.S` produce an AArch64 EFI application without an
EFI startup library or third-party DT parser. Only bounded FDT metadata is
parsed, not a general device tree. `tests.c` supplies host-only firmware doubles.

Build inputs: Debian LLVM 19.1.7 (`clang`, `lld-link`, `llvm-readobj`) and
GCC for host tests. No external source, downloaded EFI library, or linked
runtime is used. The ABI declarations in `efi.h` cover only accessed UEFI 2.x
fields; review them against firmware ABI before a board run. Run from this
directory:

```sh
make test
make
/usr/lib/llvm-19/bin/llvm-readobj --file-headers --sections --coff-basereloc /tmp/muen-efi-inventory-build/inventory.efi
sha256sum /tmp/muen-efi-inventory-build/inventory.efi
```

`OUT` can override the default `/tmp` build directory. Nothing is installed.
The build links only `core.obj`, `probe.obj`, and `current_el.obj` with
`/nodefaultlib`. Review both these objects and the disassembly for unexpected
instructions before deployment. Firmware service pointers are borrowed;
each successful `AllocatePool` is paired with `FreePool`. `HandleProtocol`
returns a borrowed loaded-image pointer (no `OpenProtocol` ownership).

ConOut prints one human banner and one ASCII JSON object followed by CRLF.
Schema `1` uses JSON decimal integers and `0x`-prefixed 16-digit hex strings;
strings escape quote and backslash and non-ASCII/control bytes as `\\u00xx`.
Unknown values use status `unavailable`, `unsupported`, `malformed`, or
`not_probed` and JSON `null` where appropriate, never a fabricated board value.
At most 12 map descriptors are serialized; `map_omitted` counts the remainder.
The fixed 2048-byte record is replaced by `{"schema":1,"truncated":true}`
if it overflows (no partial JSON is printed). The descriptor list preserves
raw unknown types and attribute bits for listed entries. The report is a
**boot-services snapshot**, not a final ExitBootServices map. DT model is a
bounded root property, not proof of a Linux DT or physical ownership.

Observations: application build/schema, firmware vendor/revision separate from
UEFI revision, loaded-image base/size and device-path pointer presence, a
bounded memory map (256 KiB maximum, five attempts), firmware configuration-
table DT metadata (1 MiB maximum), read-only global `SecureBoot` and
`SetupMode` byte values, and raw/decoded `CurrentEL`. A reported EL2 is not
independent evidence of physical EL2 or non-secure state. Policy bytes are not
fuse evidence or image-signature/enforcement proof. Missing DT is normal on
generic firmware; an invalid firmware pointer cannot be safely caught by C.

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
