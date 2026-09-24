# Task 003: first Tegra234 EL2 diagnostic experiment

This is a specification, not an implementation or execution record. It resolves
one question: can an authorized EFI image on the actual Jetson preserve NVIDIA
initialization, leave boot services, and execute a controlled single-core
non-secure EL2 payload with a trustworthy memory/DT handoff?

Evidence IDs resolve through [`003-source-index.json`](003-source-index.json);
the ownership rationale is in
[`003-tegra234-platform-contract.md`](003-tegra234-platform-contract.md).

## Gate 0: prerequisites before any target execution

Record, without changing the device:

1. module P-number/SKU and RAM size; carrier P-number and FAB/revision;
2. installed Jetson firmware/BSP version as reported by UEFI or the existing OS;
3. boot media/order and a removable or one-shot boot option that does not replace
   the known-good entry;
4. Secure Boot/fuse state and an owner-approved image-authentication method;
5. documented recovery-mode access and a tested known-good normal boot;
6. exact console connector, voltage and route from the correct carrier manual;
7. an installed-firmware DT and final EFI-map capture plan.

Stop if any step would require disabling Secure Boot, enrolling an unapproved
key, changing fuses, flashing firmware/partitions, opening `/dev/mem`, or using
an AGX connector label as Nano wiring guidance. r36.4.4 remains reference
evidence until the installed version is identified [NVIDIA-UEFI-R3644,
NVIDIA-NX-NANO-R3644].

## Artifact and load route

Use one AArch64 PE/COFF EFI application accepted by the existing UEFI policy,
loaded from removable media or a one-shot boot option. Do not overwrite
`BOOTAA64.EFI`, L4TLauncher, UEFI, the A/B chain, DTB, or normal boot variables.
The application contains or reads a tiny position-controlled payload, allocates
its pages with UEFI, and never treats unreported RAM as usable [UEFI-2.10].

Phase A remains an EFI application. Phase B occurs only after a successful
`GetMemoryMap`/`ExitBootServices` sequence and transfers through the private ABI
defined in the contract. This transition—not EFI application entry—is the EL2
experiment. NVIDIA's L4T `LoadImage`/`StartImage` path is implementation evidence
that EFI images are launched, not a guarantee about this artifact's signature
or final state [NVIDIA-EDK2-R3644].

## Phase A: firmware-resident probe

Using only UEFI protocols, emit and retain a compact, versioned record containing:

* firmware vendor/revision and loaded-image device path;
* `CurrentEL`, `MPIDR_EL1`, `MIDR_EL1`, `ID_AA64PFR0_EL1`,
  `ID_AA64MMFR0_EL1`, `ID_AA64MMFR1_EL1`, `CNTFRQ_EL0`, two counter samples,
  DAIF, `SCTLR_EL2`, `HCR_EL2`, `CNTHCTL_EL2`, `CNTVOFF_EL2`, `VBAR_EL2` and
  `ICC_SRE_EL2` only if running at EL2;
* firmware DT address, size, compatible/model, `/chosen`, `/memory`,
  `/reserved-memory`, CPU/PSCI, GIC, timer, SMMU and serial nodes;
* payload allocation and the final UEFI memory-map descriptors, descriptor size/
  version and map key;
* CRC/hash and monotonically numbered phase markers.

Send the Phase A summary through UEFI `ConOut` and keep the full record in
loader-owned pages. An EFI banner proves only EFI execution. If `CurrentEL` is
not EL2, print `STOP-NOT-EL2`, return to firmware without calling
`ExitBootServices`, and retain the observation as evidence that a higher-
privilege transition would be needed. The experiment does not try to write
EL3-owned registers.

Retrieve the final map immediately before `ExitBootServices`; retry only as
UEFI 2.10 permits when the key is stale. After success, invoke no boot service
and do not return to UEFI [UEFI-2.10].

## Phase B: bounded post-firmware payload

Run on the boot CPU only with DAIF masked. Install a payload-owned, aligned
`VBAR_EL2` before deliberate exceptions. Validate the handoff magic/version,
checksum, payload range, DT bounds and memory-map bounds. Record—before changing
them—the same architectural registers plus `TCR_EL2`, `TTBR0_EL2`, `MAIR_EL2`
and cache-line feature data.

The payload may:

1. prove EL2 by reading `CurrentEL`;
2. prove the architectural counter advances and report `CNTFRQ_EL0`;
3. validate that its code, stack, handoff and output buffer occupy loader-owned
   memory and do not overlap UEFI runtime/reserved or DT reserved ranges;
4. install vectors and execute one synchronous, self-contained exception test
   only after the vector table is active;
5. write a fixed banner and full result to the preallocated RAM record;
6. use post-exit serial only if Gate 0 separately established that exact route
   and its clock/reset/mailbox service remains usable.

It must not enable interrupts, write GIC distributor/redistributor/interface
state, arm timer PPIs, call PSCI, release CPUs, touch SMMU registers, start DMA,
initialize devices, install stage-2 translation, run Muen, or run a subject.
Keeping the inherited EL2 stage-1 mapping for this diagnostic minimizes change;
the payload records it but does not pretend that it satisfies Muen's MMU-off
entry assumption [MUEN-KERNEL].

If no proven post-exit transport exists, Phase B ends in a bounded wait/watchdog
or reset path selected before execution. RAM output is not considered observed
until recovered by an approved mechanism that does not assume persistence.

## Higher-privilege/firmware evidence versus payload observations

The payload can observe EL, architectural ID/timer/translation registers,
MPIDR, the supplied DT/map, and whether its vectors execute. It cannot establish
EL3 interrupt grouping/security controls, secure SMMU ownership, hidden
carveouts, DMA quiescence, firmware's intended service lifetime, fuse state, or
the correctness of carrier routing. Those require vendor documentation,
installed-firmware source/configuration, or separately approved higher-
privilege evidence. Readable MMIO is not proof of ownership.

## Stop and recovery rules

Stop before Phase B on authentication failure, unexpected EL, invalid map/DT,
overlap, unsupported feature/granule, missing output/recovery path, inability to
disable the UEFI watchdog safely, or any divergence from the inventoried board.
In Phase B, any unexpected exception records the syndrome if vectors work and
then stops; there is no exploratory MMIO fallback.

The normal boot entry and media remain unchanged. Remove the test media or let
the one-shot option expire to recover. Keep NVIDIA recovery-mode instructions
available, but do not flash as part of this experiment. A power-cycle/reset is
acceptable only under the pre-agreed board procedure.

## Acceptance criteria and limits

**EFI-execution success:** an authenticated image runs and its Phase A record
identifies firmware, board-visible DT/map and actual entry EL.

**EL2-environment success:** after successful `ExitBootServices`, the same boot
CPU validates the handoff at non-secure EL2, the counter advances at the reported
frequency, payload-owned vectors handle the planned exception, memory ranges do
not overlap reported reservations, and a checksum-valid result is retrieved.

Failure of Phase B after Phase A is still useful evidence and must not be
papered over. Success establishes only a usable single-core diagnostic handoff.
It does **not** establish interrupt ownership, SMMU/DMA isolation, multicore,
Muen execution, a runnable Linux subject, or subject isolation.

## Explicitly later work

Only after this milestone may a next design map GICv3 ownership and timer PPIs,
then separately model all three SMMUs and active DMA masters. Porting Muen,
multicore, device passthrough, Linux-on-Orin, GPU/CUDA and proof work are outside
this experiment.
