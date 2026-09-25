# Task 004: firmware-resident inventory probe

Starting `main`: `1830aa164e19bb046aa9eabbe4537bb0ba7e82fe`.
PR #3 merged at this commit with no tree differences from reviewed
`a5ee190214d24b742e075370341e7779a6402090`. The local main was
fast-forwarded; the Task 004 branch was created from it. No Orin access.

The implementation and output contract are in
[`../../tools/efi-inventory/README.md`](../../tools/efi-inventory/README.md).
Host tests exercise parser boundaries, arithmetic, missing policy variables,
EL decoding, map allocation/retry cleanup and forbidden-call sentinels. They
do not validate NVIDIA firmware behavior. The freestanding app uses host LLVM
19.1.7, no EDK2 binary/library or extra source download; EDK2 in Task 003 is
a firmware *reference*, not an application build dependency.

Build: `cd tools/efi-inventory && make test && make` (output in `/tmp`).
Host doubles and JSON decoding passed; existing tests passed with
`PYTHONPATH=. /home/zboll/.local/share/muen-orin/task002-venv/bin/pytest -q tests`
(3 passed). Pre-commit checks passed. Built image SHA-256:
`4c95bd70bc487c9771ee2efddeac228f8d93f1d7dbe34ff05359596e5e1e703f`.
`llvm-readobj` identifies ARM64 `0xaa64`, EFI application subsystem `0x0a`,
`.text`, `.rdata`, `.reloc`, DIR64 base relocations, and no import table.
Disassembly has one system-register instruction, `mrs x0, CurrentEL`.
Generic AArch64 UEFI emulator smoke test was **not run**: no AAVMF/QEMU_EFI
firmware image was available locally; the existing Xilinx QEMU is not an
AArch64 UEFI firmware environment. This is not Orin validation. Existing
Muen/Xilinx runtime baseline and recipe/submodule pins remain unchanged.
Hardware checklist and prohibited operations are in the probe README; a
cross-build satisfies none of the Task 003 post-exit gates.
