# Task 003: first Tegra234 EL2 diagnostic experiment

This is a specification, not an implementation or execution record. It asks
whether one owner-approved AArch64 EFI image can preserve NVIDIA initialization,
leave boot services, and enter a bounded single-core diagnostic at EL2. No Orin
was accessed. Evidence IDs resolve through
[`003-source-index.json`](003-source-index.json); ownership rationale is in
[`003-tegra234-platform-contract.md`](003-tegra234-platform-contract.md).

## Gate 0: prerequisites

Record without changing the device: module SKU/RAM and carrier revision;
installed BSP/firmware; a removable or one-shot boot route; fuse/Secure Boot and
installed image-execution policy; an owner-approved signing/authorization path;
known-good normal boot and recovery-mode access; exact safe console route; and a
firmware DT/final EFI-map capture plan. Stop rather than disable Secure Boot,
enroll an unapproved key, change fuses, flash, use `/dev/mem`, or infer Nano
wiring from AGX documentation [NVIDIA-UEFI-R3644, NVIDIA-NX-NANO-R3644].

Owner approval, firmware acceptance, cryptographic authentication, and record
integrity are separate claims. Owner approval authorizes the experiment.
`LoadImage`/`StartImage` acceptance demonstrates only acceptance by the policy
actually installed. It demonstrates cryptographic image authentication only
when that policy and its enforcement were independently established. A record
CRC detects accidental corruption; it authenticates neither producer nor
contents and proves neither isolation nor execution integrity [UEFI-2.10].

## One-image artifact and placement contract

Milestone 1 uses exactly one AArch64 PE/COFF EFI application. Phase A and the
Phase B diagnostic are linked into that same image; all diagnostic instructions
and the EL2 vector table reside in executable image sections covered by the
image authentication operation. There is no external raw executable payload,
runtime code copy, or private executable allocation. An external-payload design
is deferred and would require a separate reviewed authentication, executable-
mapping, relocation, and cache-maintenance contract.

The firmware image loader supplies the image-section mappings and permissions.
The application must verify from its loaded-image metadata that the Phase B and
vector ranges are within the loaded image and must not make a data allocation
executable or disable firmware memory protections. Allocate stack, handoff,
copied final map, DT snapshot if required, and result records as writable
`EfiLoaderData`; none contains executable code. The image's `EfiLoaderCode` and
these `EfiLoaderData` pages remain allocated and mapped throughout Phase B.
Because no code is copied, no code-copy cache maintenance is required. The
diagnostic retains the inherited identity mappings required by UEFI 2.10
section 2.3.6 and neither replaces nor reclaims the active translation tables or
any backing memory during milestone 1 [UEFI-2.10].

## Register-access policy

An `MRS` is not intrinsically safe: architectural controls can make a read trap
or be UNDEFINED. DAIF masks asynchronous classes, not synchronous system-register
traps. An EL2 vector cannot recover an exception routed to EL3. Phase A therefore
performs the mandatory set below before the exit sequence; Phase B performs no
exploratory register read before switching immediately to the owned stack/vector
context. Milestone 1 has no optional register probes: every non-mandatory field
below is explicitly `not probed`. A later plan may promote one only after review
of its exact feature, EL and higher-privilege access pseudocode against matching
installed-firmware evidence [ARM-ARM-2025-06, ARM-GIC-IHI0069H].

| Register/operation | Purpose and architectural feature | Required EL, higher controls and possible trap | M1 policy if permission is not established |
|---|---|---|---|
| `CurrentEL` | Report the current exception level; FEAT_AA64 | Read at EL1 or higher and UNDEFINED at EL0. Under nested virtualization an EL1 read with effective `HCR_EL2.NV` set can report EL2, so the value alone is not permission evidence | **Mandatory Phase A.** If not EL2, emit `STOP-NOT-EL2`. If EL2, continue only when matching firmware evidence excludes an emulated EL1 result and establishes real EL2 execution; otherwise stop before exit. Phase B does not re-probe before vectors. |
| `SP` selection and `DAIFSet` | Enter the owned stack with asynchronous exceptions masked; base AArch64 | Executed at the current EL. DAIF does not mask synchronous exceptions or make later accesses safe | **Mandatory transition.** Establish ordinary EL2 execution first; otherwise do not exit. No DAIF observation is proposed. |
| `VBAR_EL2` write | Install the aligned in-image EL2 vector table; FEAT_AA64 and EL2 | The DDI 0601 (2025-06) access pseudocode permits the write at EL2; at EL1 it is UNDEFINED or, under nested virtualization controls, can trap to EL2. The vector address also has architectural alignment/address constraints | **Mandatory transition prerequisite.** Establish real EL2 execution and a conforming vector address before exit; otherwise stop in Phase A. Do not test permission after exit. No `VBAR_EL2` read is proposed. |
| `ICC_SRE_EL2` | GICv3 system-register-interface enable state; GICv3 system-register interface | EL2-only, but `ICC_SRE_EL3.Enable == 0` causes EL2 access to trap to EL3 under the GIC access pseudocode. `CurrentEL == EL2` is insufficient | **Omitted in milestone 1:** always record `not probed` unless a later reviewed plan first establishes EL3 controls. Never probe GIC MMIO as fallback. |
| `MPIDR_EL1` | Boot-CPU affinity; FEAT_AA64 | EL1+ architectural ID register; exact higher-control/access pseudocode was not inspected for implementation | Deferred; `not probed`. Use DT topology only. |
| `MIDR_EL1` | PE implementer/part/revision; FEAT_AA64 | EL1+ architectural ID register; exact higher-control/access pseudocode was not inspected for implementation | Deferred; `not probed`. |
| `ID_AA64PFR0_EL1` | AArch64 PE feature/EL presence; FEAT_AA64 | EL1+ feature ID register; exact higher-control/access pseudocode was not inspected for implementation | Deferred; `not probed`. |
| `ID_AA64MMFR0_EL1`, `ID_AA64MMFR1_EL1` | Physical-address, granule and memory-model features; FEAT_AA64 | EL1+ feature ID registers; exact higher-control/access pseudocode was not inspected for implementation | Deferred; each `not probed`. No translation change is attempted. |
| `CNTFRQ_EL0` | Firmware-reported nominal counter frequency; generic timer | System-counter feature; exact EL3 timer/ECV access controls and trap destination were not inspected for implementation | Deferred; `not probed`. It would report, not independently measure, frequency. |
| `CNTPCT_EL0` | Physical counter progress; generic timer | System-counter feature; exact EL2/EL3 timer/ECV controls and trap destination were not inspected for implementation | Deferred; both proposed samples are `not probed`; no progress claim. |
| `DAIF` read | Incoming asynchronous exception masks; AArch64 PSTATE | EL1+ PSTATE access; exact access pseudocode was not inspected. DAIF never masks synchronous traps | Deferred; `not probed`. `DAIFSet` remains a separate mandatory transition instruction. |
| `SCTLR_EL2` | Incoming EL2 MMU/cache/control state; FEAT_AA64 and EL2 | EL2 control register; exact EL3 controls/trap destination were not inspected for implementation | Deferred; `not probed`; inherited translations are retained. |
| `TCR_EL2`, `TTBR0_EL2`, `MAIR_EL2` | Incoming EL2 translation regime; FEAT_AA64 and EL2 | EL2 translation registers; exact feature variants, EL3 controls and trap destinations were not inspected | Deferred; each `not probed`; active tables/backing pages are retained. |
| `HCR_EL2` | EL2 virtualization/host controls; FEAT_AA64 and EL2 | EL2 control register; exact EL3 controls/trap destination were not inspected for implementation | Deferred; `not probed`; no virtualization control is changed. |
| `CNTHCTL_EL2`, `CNTVOFF_EL2` | EL1 timer access and virtual counter offset; generic timer and EL2 | EL2 timer registers; exact EL3 timer/ECV controls and trap destinations were not inspected | Deferred; each `not probed`; no timer control is changed. |
| `VBAR_EL2` read | Incoming EL2 vector base; FEAT_AA64 and EL2 | DDI 0601 permits the read at EL2 and makes EL1 behavior UNDEFINED or nested-virtualization-controlled | Deferred; `not probed`. This is distinct from the mandatory write above. |
| GIC/SMMU/UART MMIO | Controller identity/state or output | Device mapping, security attribution, clocking and ownership are platform controls; access can abort or have side effects | Not probed. No exploratory MMIO and no writes to enable higher-privilege access. |

The primary locators are Arm A-profile register descriptions DDI 0601
(2025-06), `CurrentEL` and `VBAR_EL2` access pseudocode, and GIC Architecture
Specification IHI 0069H §12.4.11, Table 12-10 and `ICC_SRE_EL2` access
pseudocode. These exact primary texts were inspected through Arm's official
documentation service; deferred registers were not reviewed for implementation.

## Phase A and final map/exit state machine

Using UEFI protocols, build a versioned record containing firmware and loaded-
image identity, the mandatory/deferred register fields above, firmware DT and
relevant nodes, image/stack/handoff/result ranges, the final memory-map
descriptors and map metadata, monotonic phase markers, and a CRC. `ConOut` may be
used for the pre-exit summary. Finish all protocol use, output preparation,
allocations, image/range validation, payload preparation, and watchdog policy
before beginning the final map/exit sequence.

The state machine is:

1. **Prepared:** all resources and output are complete. Obtain `GetMemoryMap`.
2. **Exit attempt:** make no intervening allocation or protocol call; call
   `ExitBootServices(ImageHandle, MapKey)`.
3. **One permitted recovery:** if the first call returns `EFI_INVALID_PARAMETER`,
   call only `GetMemoryMap` and `ExitBootServices` again with the new key. Do not
   use `ConOut`, device-handle protocols, allocation, or ordinary logging in this
   restricted path. Any other failure, or second failure, follows the pre-agreed
   reset/recovery procedure.
4. **Exited:** after success, call no boot service and no device-handle protocol,
   and never return to firmware. Immediately mask DAIF and branch to a tiny
   in-image transition stub that selects the preallocated aligned stack, installs
   the in-image aligned `VBAR_EL2`, validates handoff bounds/CRC, and enters the
   diagnostic. There is no intervening exploratory probe [UEFI-2.10].

## Phase B: bounded diagnostic

On the boot CPU only, with inherited EL2 stage-1 mappings retained, validate the
handoff and confirm code, vector, stack, map, DT and output ranges remain within
retained image/loader pages and outside reported runtime/reserved and DT-reserved
ranges. Write a fixed banner/result to the preallocated RAM record. A deliberate,
self-contained synchronous exception may be executed only after `VBAR_EL2` is
active; it tests that local EL2 vector path, not recovery from exceptions routed
to EL3. Post-exit serial is disallowed for milestone 1 unless a later reviewed
contract establishes the exact route and retained service; RAM is not considered
observed until recovered by an approved mechanism.

Do not enable interrupts, alter GIC/SMMU/security/timer controls, arm a timer PPI,
call PSCI, release CPUs, start DMA, initialize devices, install stage 2, reclaim
inherited tables/buffers, run Muen, or run a subject. If an unexpected local
exception reaches the installed vector, record syndrome and stop. There is no
MMIO fallback. A preselected watchdog/reset mechanism must not require boot
services after exit [MUEN-KERNEL].

## Evidence and acceptance limits

`CurrentEL` reports EL only. The non-secure assumption requires separate matching
firmware/security evidence because `CurrentEL` does not report Security state.
The payload cannot establish EL3 interrupt grouping, secure SMMU ownership,
hidden carveouts, DMA quiescence, firmware-service lifetime, fuse state, or
carrier routing. Readable state is not proof of ownership.

**EFI-stage success** means the owner-approved image was accepted and ran, the
actual entry EL was recorded, and board-visible DT/map data were captured. It is
not a signature-verification claim absent established policy. **EL2 diagnostic
success** means the same CPU completed the controlled post-exit transition,
owned ranges validated, the local vector test completed, and a CRC-valid record
was retrieved. CRC validity proves only corruption detection, not authenticity,
isolation, interrupt ownership, SMMU/DMA containment, multicore operation, Muen,
or Linux-subject execution.

Stop before exit on unexpected EL, invalid map/DT/ranges, missing recovery/output
path, or unmet inventory/authentication conditions. Later work may separately
design GICv3/timer ownership, SMMU/DMA containment, multicore, Muen, Linux, or an
external payload. This document stops at the review gate.
