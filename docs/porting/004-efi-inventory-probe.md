# Task 004: firmware-resident inventory probe

Starting `main`: `1830aa164e19bb046aa9eabbe4537bb0ba7e82fe`.
PR #3 merged at this commit with no tree differences from reviewed
`a5ee190214d24b742e075370341e7779a6402090`. The local main was
fast-forwarded; the Task 004 branch was created from it. No Orin access.

The implementation and output contract are in
[`../../tools/efi-inventory/README.md`](../../tools/efi-inventory/README.md).
Host tests exercise the application through firmware doubles; they do not
validate NVIDIA firmware behavior or the firmware calling convention. The
freestanding app uses host LLVM 19.1.7 and no EDK2 binary, library or
source at build time. EDK2 headers at a pinned commit were read only as the
ABI review reference (see below); they are not a build dependency.

## Initial submission (`918a20e9ad2021788015ea8de7317482bb18375d`)

Image SHA-256 `4c95bd70bc487c9771ee2efddeac228f8d93f1d7dbe34ff05359596e5e1e703f`.
Superseded by the corrective revision below; review found the defects
listed there.

## Corrective revision

Defects reproduced on the reviewed head and corrected:

1. **GetVariable sequencing.** `parse_var(get_variable(..., &size, &value),
   size, value)` read `size`/`value` in the same call as the service that
   writes them; argument evaluation order is unspecified. Reproduced: a
   double writing SecureBoot=1/SetupMode=1 was reported as `0` with GCC
   14.2 `-O2` and as `1` with Clang 19.1.7. Now the call is a separate
   statement. Regression `t_var_secure1_setup0` fails under GCC when the old
   expression is restored and passes with the fix.
2. **Memory-map over-read.** With a 4096-byte pool buffer, a `SUCCESS`
   response of size 4120/stride 40 made `parse_map` read past the buffer
   (ASan heap-buffer-overflow at `core.c:20`). `parse_map` now takes the
   allocated capacity and rejects `size > capacity` before reading
   (`malformed`/`size_exceeds_buffer`, released once). Removing the check
   reproduces the ASan finding under GCC and Clang.
3. **DT fixture and parser.** The "valid" test tree had
   `off_mem_rsvmap=48` inside the structure block `[40,56)`. The parser
   claimed v16 while reading the v17-only `size_dt_struct`, did not check
   alignment, overlap, reservation-map termination or single-root
   structure, and reported a valid model longer than 64 bytes as a
   malformed tree. The parser is now a narrowly specified v17 reader
   (`version >= 17`, `last_comp_version <= 17`), separates model
   truncation from tree validity, and uses dtc-generated fixtures.
4. **FirmwareVendor.** CHAR16 was cut at the first non-ASCII unit (possibly
   an empty observed string) with no truncation marker. Units are now
   `\uXXXX`-escaped, surrogates are `unsupported`, and names over 64 units are
   `truncated`.
5. **Configuration-table search.** A DT beyond the first 256 entries was
   reported `unavailable`; it is now `not_probed`/`search_limit`.
6. **Output/return status.** `efi_main` returned success even when every
   `OutputString` call failed. It now stops at the first error and returns
   it, and returns `EFI_UNSUPPORTED` without ConOut and
   `EFI_BUFFER_TOO_SMALL` when only the truncation marker fits.
7. **Build dependencies.** Objects did not depend on `efi.h`/`core.h`.
   `-MMD -MP` dependency files now track them.
8. **ABI review.** `EFI_SYSTEM_TABLE` had a redundant explicit `padding`
   member (layout-neutral under natural alignment). Tables are now declared
   in full against EDK2 `2f1971dd4d770c418f9aadb60b18c5645af3344c` with
   compile-time offset/size assertions on target and host builds.

The record schema is now `2` (per-observation `status`/`detail` objects);
see the probe README for the parser, encoding, output and error contracts.

### Validation performed

- `make test`: 94 named cases with ASan+UBSan
  (`-fno-sanitize-recover=all`) under GCC 14.2.0 and Clang 19.1.7, each at
  `-O0` and `-O2`, all passing; plus a `RECORD_CAPACITY=512` build for the
  truncation path. `check_records.py` decoded 66 captured records per variant
  with a strict JSON parser; all passed. The largest record was 2699 of
  4096 bytes.
- Mutation checks, run once by hand: restoring the old GetVariable expression fails
  `t_var_secure1_setup0` on GCC (Clang passes, as with the original
  defect); removing the capacity check gives an ASan heap overflow on both
  compilers; removing the search-limit status, the DT overlap check, or
  OutputString error handling fails the corresponding named case.
- Forbidden-call sentinels fill every unused boot-service, runtime-service
  and text-output slot, including ExitBootServices, SetVirtualAddressMap,
  SetVariable, ResetSystem, AllocatePages, SetWatchdogTimer and
  OpenProtocol. Every case asserts zero sentinel calls and exactly-once
  release of each allocation.
- `make fixtures-check DTC=<bob devel/dtc DTC 1.6.1>`: regenerated fixtures
  identical.
- Build tracking: clean build compiled 3 objects and linked; the unchanged
  second build printed "Nothing to be done" (`make -q` exit 0); touching
  `efi.h` and, separately, `core.h` rebuilt `core.obj` and `probe.obj`
  and relinked. Changing an ABI offset assertion failed both target and
  host compiles.
- Existing tests: `PYTHONPATH=. <task002-venv>/bin/pytest -q tests`
  (3 passed). Pre-commit hooks passed on the changed files.

### Corrected EFI artifact

Toolchain: Debian clang/LLD 19.1.7, `--target=aarch64-pc-windows-msvc`,
`/timestamp:0`. `make` output
`/tmp/muen-efi-inventory-build/inventory.efi`, 18432 bytes, SHA-256
`43ee0b73ea66461914aba59328e65eb88f4288e81aac834375d5b8755995aa68`
(reproduced in a fresh output directory). `llvm-readobj`: machine ARM64
`0xAA64`, subsystem EFI application `0xA`, DYNAMIC_BASE/NX_COMPAT; sections
`.text` (read/execute), `.rdata` (read), `.data` (read/write, 4 KiB report
buffer, not executable), `.reloc` (discardable); no import table; 9 DIR64
base relocations. `llvm-objdump -d`: the only system-register, barrier,
cache/TLB, exception or wait instruction is `mrs x0, CurrentEL`; no
`__chkstk`. The objects import no external symbol.

### Remaining limitations

Generic AArch64 UEFI emulator smoke test **not run**: no AAVMF/QEMU_EFI
firmware image or `qemu-system-aarch64` is available locally, and host
doubles are not a substitute. Nothing was run on Orin; the hardware gate
in the probe README is unchanged. Existing Muen/Xilinx runtime baseline and
recipe/submodule pins are unchanged, and a cross-build satisfies none of the
Task 003 post-exit gates.
