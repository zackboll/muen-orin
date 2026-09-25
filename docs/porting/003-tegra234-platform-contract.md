# Task 003: Tegra234 platform and firmware contract

Date: 2026-09-24

## Scope, revisions, and evidence language

This is source research and design for an intended **Jetson Orin Nano 8GB /
Tegra234 / Cortex-A78AE**. No Orin was accessed. Carrier revision, installed
firmware, board SKU/FAB, boot media, fuse/security state, and usable physical
console are unknown. The only observations of a running Muen ARM64 system remain
the Task 002 QEMU results; Task 001 supplies the pinned source baseline
[TASK-001, TASK-002].

The study starts at `main` `10f2c015e370a30830e49d10c89dd04afbea4cac`.
PR #2's reviewed head `563fed78e19531f445bed11ebd34287d6456ca49`
is its parent; the intervening merge commit has no additional content. Muen pins
remain kernel `bb48b4c5...` and systems `99f9e626...` [MUEN-KERNEL,
MUEN-SYSTEMS]. The coherent vendor reference is **Jetson Linux r36.4.4**, with
`edk2-nvidia` tag `uefi-202405.3` at `350c50a65...` and matching NVIDIA EDK2
tag at `338f5d9e7...` [NVIDIA-EDK2-R3644, NVIDIA-EDK2-BASE-R3644]. This is a
reference set, not a claim that the target runs r36.4.4.

Terms used below are strict:

* **Guarantee** is an explicit specification or vendor-document statement.
* **Source observation** is behavior in the cited revision, not a promise for
  another build or installed device.
* **Proposed contract** is a condition a future loader/payload must enforce.
* **Unknown** requires target-specific evidence. Linux DT is implementation
  evidence, never substituted for firmware ownership [LINUX-V6.1-T234].

Stable citation records and hashes are in
[`003-source-index.json`](003-source-index.json).

## Boot chain and candidate handoff

### What the selected sources establish

NVIDIA documents the T23x cold-boot chain as BootROM/PSCROM, MB1, MB2 and UEFI.
MB1 consumes platform BCT data; MB2 and earlier firmware preserve NVIDIA's
memory, power, clock and security initialization before UEFI [NVIDIA-BOOT-R3644,
NVIDIA-NX-NANO-R3644]. Replacing that chain is therefore neither required nor
recommended.

The matching source adds material detail:

1. `PrePi/AArch64/ModuleEntryPoint.S:48` invokes `EL1_OR_EL2_OR_EL3(x1)` from
   `AsmMacroIoLibV8.h:29-35`. The macro creates three paths: EL1 branches to
   label `1`, EL2 to label `2`, and EL3 to label `3`; labels `1` and `2` join the
   common path at lines 119-122. Only EL3 executes lines 50-117: it loads
   `SPSR_EL3`, writes `SCR_EL3` and `CNTFRQ_EL0`, initializes GIC system-register,
   grouping and affinity-routing state, then `eret`s into the common path
   [NVIDIA-EDK2-R3644, NVIDIA-EDK2-BASE-R3644]. The EL1 and EL2 paths execute
   none of those operations. The source contains all three entry paths, so it
   does not establish which preceding-firmware path T234 actually selects.
2. EDK2 declares default values for `PcdArmNonSecModeTransition` (`0x3c9`) and
   `PcdArmScr` (`0x501`), while NVIDIA source supplies a configured timer value.
   Defaults and source configuration are not effective build values until the
   complete platform DSC/FDF and build report prove selection and overrides.
   Even an executed EL3 path would describe initialization at that point, not
   final UEFI GIC, security, or timer state [NVIDIA-EDK2-R3644,
   NVIDIA-EDK2-BASE-R3644].
3. PrePi obtains the UEFI carveout and boot parameters from the preceding CPU
   bootloader, constructs the memory/resource HOBs, reserves firmware and DTB,
   enables its own MMU, registers the firmware DT, and enters DXE. Its T234
   resource code carries forward firmware carveouts and VPR rather than
   treating all DRAM as free [NVIDIA-EDK2-R3644]. This is interface-consuming
   source, not accessible matching evidence for the actual MB2/secure-firmware
   path or the EL selected at handoff; that path-selection evidence is unavailable.
4. NVIDIA documents UEFI as the rel-34-and-later bootloader and supports normal
   UEFI boot options. The r36.4.4 L4T launcher parses `extlinux.conf`, applies
   DT overlays, uses UEFI `LoadImage`, supplies load options, then calls
   `StartImage` [NVIDIA-UEFI-R3644, NVIDIA-EDK2-R3644]. Thus a PE/COFF AArch64
   EFI application on an enabled boot device is the best-supported ingestion
   format. A raw Muen ELF is not established as an accepted firmware image.
5. UEFI application entry receives `ImageHandle` and `SystemTable`. It is still
   a firmware client: boot services and the firmware's mappings/state remain in
   force. A loader must obtain the final map and matching key immediately before
   successful `ExitBootServices`; afterward it may reclaim only types permitted
   by UEFI and must preserve runtime/reserved regions [UEFI-2.10]. EFI entry is
   therefore not synonymous with a post-firmware Muen entry.

### Separate UEFI architectural platform contract

UEFI 2.10 section 2.3.6 is normative for an AArch64 UEFI execution environment:
Boot Services execute in little-endian AArch64 at EL1 or EL2, with the MMU and
instruction/data caches enabled and RAM described by the UEFI map identity
mapped; section 2.3.6.2 defines the handoff state and section 2.3.6.3 constrains
alternate translations [UEFI-2.10]. This specification requirement is distinct
from NVIDIA source observations and from the still-unobserved installed board.
It permits either EL1 or EL2 and therefore does not resolve the PrePi path.

### Best-supported route

The candidate route remains **NVIDIA's existing chain -> UEFI -> one authorized
AArch64 EFI image -> `ExitBootServices` -> its embedded EL2 diagnostic**. Phase A,
the transition stub, vectors, and Phase B code are executable sections of the
same authenticated PE/COFF bytes. Writable stack, handoff, copied final map/DT
data and result records are loader-data allocations and are never assumed
executable. There is no external raw payload or code copy in milestone 1.

Entry arguments to the EFI image are the UEFI pair, not Linux's `x0 = DTB` and
not Muen's current no-argument entry. The proposed private payload ABI is:

* `x0`: physical address of a page-aligned, loader-owned handoff structure;
* `x1`-`x3`: zero;
* structure: size/version, copied final UEFI memory map and descriptor metadata,
  firmware DT physical address/size, EFI-stage observations, payload range,
  selected output method, and checksum;
* one boot CPU only; secondaries remain under firmware/PSCI control;
* after successful exit, immediately select the owned stack and install the
  in-image `VBAR_EL2`, with no intervening exploratory probe;
* the sole observational register read is `CurrentEL`; other considered
  system-register fields, including `ICC_SRE_EL2`, are `not probed`; permission
  for the mandatory stack/DAIF/vector transition must be established pre-exit.

Linux's documented AArch64 entry convention is useful comparison evidence but
is not adopted merely because L4T boots Linux [LINUX-AARCH64-BOOT].

No executable code is copied, so code-copy cache maintenance is unnecessary.
Milestone 1 retains UEFI's identity mappings, loaded image, loader-data pages,
active translation tables and their backing memory for its lifetime; it does
not disable the MMU or reclaim inherited tables. This differs from current
Muen's MMU-off entry contract, and the diagnostic does not jump into Muen
[UEFI-2.10, MUEN-KERNEL].

### Conditions not yet established

The route is a candidate, not yet a supported target procedure, until all of
these are known: actual board identity; installed UEFI version; whether its boot
manager exposes a non-destructive removable-media/boot-option path; Secure Boot
state, installed image policy and an owner-approved image; actual EFI entry EL
and separate evidence for non-secure state; successful `ExitBootServices`; final
memory map/DT; and a physically routed,
electrically safe console. No evidence here authorizes changing keys, fuses,
firmware, or boot partitions.

## Ownership and initialization contract

| Area | Current Muen assumption | r36.4.4/T234 evidence | Proposed EL2 responsibility | Blocking unknown |
|---|---|---|---|---|
| GICv3 | Muen instantiates GIC-400/GICv2, requires all interrupts disabled/non-secure, validates a GIC-400 IIDR, and uses GICC/GICH/GICV MMIO [MUEN-KERNEL]. | Only PrePi's conditional EL3 path enables ICC system registers, changes groups and affinity routing. The EL1/EL2 paths do not. Linux describes GICv3 topology, not final firmware state [NVIDIA-EDK2-R3644, LINUX-V6.1-T234]. | Record DT ranges only. `ICC_SRE_EL2` is `not probed`: GIC IHI 0069H specifies an EL3 trap when `ICC_SRE_EL3.Enable == 0`, so EL2 is insufficient permission evidence. No GIC MMIO or writes. | Actual path, final groups/enables/routes, maintenance/timer PPIs and EL3 controls. Blocks interrupts and Muen scheduling. |
| Timer | Reads `CNTFRQ_EL0`, programs CNTHP at EL2, changes `CNTHCTL_EL2`, and assumes policy IRQ constants/routing [MUEN-KERNEL]. | The conditional EL3 path writes the configured `PcdTegraArchTimerFreqInHz`; `ArchPrePi.c:24-34` changes `CNTHCTL_EL2` only when then running at EL2. Neither establishes effective build values or final target state. Linux describes timer topology only [NVIDIA-EDK2-R3644, LINUX-V6.1-T234]. | Timer reads are optional and only after matching access evidence. If read, two counter samples prove progress and `CNTFRQ_EL0` reports nominal frequency; they do not independently measure frequency. Do not change controls or enable an interrupt. | Effective frequency, EL3 trap controls, PPI group/priority/route. Blocks timer IRQ/scheduler tick, not EFI-stage success. |
| CPU/PSCI | Direct EL2 startup requires core 0 and every policy CPU already running; affinity extraction is ZynqMP-oriented [MUEN-KERNEL]. | T234 DT advertises PSCI 1.0 and MPIDRs; UEFI derives enabled-core records from bootloader/fuse data [LINUX-V6.1-T234, NVIDIA-EDK2-R3644]. | Use only the boot CPU; record PSCI DT node/conduit but do not probe `MPIDR_EL1` in milestone 1. Make no `CPU_ON` call. Preserve firmware PSCI for later secondary release. | Actual primary affinity, enabled six-core SKU map, conduit and post-exit PSCI functionality. Blocks multicore only. |
| RAM/translation | Muen expects MMU off, installs EL2 stage-1 and guest stage-2 tables from static policy, and assumes a static ZynqMP map [MUEN-KERNEL, MUEN-SYSTEMS]. | PrePi builds resources from bootloader-provided DRAM/carveouts; UEFI 2.10 section 2.3.6 separately requires identity-mapped described RAM during Boot Services and Table 7.6 defines post-exit ownership [NVIDIA-EDK2-R3644, UEFI-2.10]. | Keep code/vectors in the loaded image and writable state in loader data. Retain inherited mappings, active tables/backing pages, runtime/reserved and DT-reserved memory. Do not copy code, alter translations, or reclaim anything still used. | Actual 8GB map, carveouts, translation-table ownership and firmware runtime dependencies. Blocks Muen policy and translations. |
| SMMU/DMA | Muen has one SMMU-500/SMMUv2 model and requires all context banks/SMRs non-secure; bundled BL31 temporarily bypasses then initializes it [MUEN-KERNEL]. | Linux v6.1 describes **three** `nvidia,tegra234-smmu`/`nvidia,smmu-500` controllers (`iso`, `niso0`, `niso1`) and per-device SIDs. UEFI has active USB/display/storage/network paths and a carveout named for CCPLEX SMMU page tables, but reviewed public source does not establish final global bypass, security assignment, or quiescence [LINUX-V6.1-T234, NVIDIA-EDK2-R3644, ARM-SMMUV2]. | Do not program or infer SMMU state. Preserve mappings/resources, avoid assigning devices, and stop before reclaiming DMA buffers. A later design must inventory every active master, SID, controller, clock/reset and secure ownership. | Final SMMU registers/security, SID routing, firmware-managed DMA and quiescence. Blocks DMA isolation and therefore any isolation claim. CPU stage-2 is not DMA isolation. |
| Console | Current debug server and ZynqMP UART policy are not a T234 early console [MUEN-SYSTEMS]. | UEFI selects serial by firmware DT. TCU uses combined-UART TX/RX mailboxes; 16550 is a separate path. UARTA has clock/reset/pinmux dependencies in Linux DT. Carrier adaptation changes pinmux/routing [NVIDIA-EDK2-R3644, NVIDIA-NX-NANO-R3644, NVIDIA-ORIN-FEATURES-R3644, LINUX-V6.1-T234]. | Use EFI `ConOut` for the pre-exit report. Post-exit output is permitted only after the exact board route and either a firmware-independent UART setup or an explicitly retained TCU service are proven. Otherwise write a checksum-protected RAM record and recover it only through an approved later boot method. | Board/carrier connector, voltage, pinmux, clocks/resets, TCU mailbox service lifetime and post-exit availability. Blocks visible post-exit banner, not EFI-stage evidence. USB gadget serial is not accepted as EL2 output. |

### Firmware services to preserve

PSCI is expected to remain the power-management owner if exposed by the final
DT. UEFI runtime code/data and any mapped runtime MMIO must be preserved if
runtime services are retained [UEFI-2.10]. NVIDIA BPMP/SPE/secure-world mailbox,
clock, reset, thermal and power services are not reimplemented or disabled.
Their exact post-exit interfaces and dependencies are unknown; absence of a
guest device assignment says nothing about their DMA activity.

## Comparison to Muen startup

The present binary cannot be used unchanged. Its EL2 path assumes Cortex-A53,
MPIDR/core 0, MMU off, all policy cores already running, GIC-400 state and a
single SMMU-500 ownership contract. It installs vectors/translations and starts
kernel scheduling rather than ingesting an EFI map/DT [MUEN-KERNEL]. Tegra234
instead provides source evidence for GICv3, three SMMU-500-compatible instances,
firmware-derived memory carveouts, PSCI-managed CPUs and firmware-dependent
serial. A shim cannot turn these missing drivers/policies into compatibility;
its first purpose is to measure and preserve the handoff.

## Established facts

* NVIDIA documents the T23x chain reaching UEFI. The selected PrePi source has
  distinct EL1, EL2 and EL3 entry paths; only EL3 performs SCR/SPSR, CNTFRQ and
  GIC initialization. Accessible evidence does not identify T234's actual path.
* UEFI can load/start AArch64 EFI images; this establishes an ingestion path,
  not automatic acceptance or signature verification under unknown policy.
* Firmware-derived memory and carveout data must survive into the handoff.
* Tegra234 uses GICv3 and Linux implementation data describes three SMMUv2/
  SMMU-500-compatible controllers; Muen implements GICv2 and one SMMU model.
* The EL3 source path programs the architectural frequency from a configured
  value. That does not establish the effective build value or final target state.
* TCU is mailbox based and distinct from a directly programmed 16550 UART;
  physical console routing remains carrier-specific.

## Critical unresolved assumptions

Installed release/security policy, board/carrier identity, EFI acceptance and
cryptographic enforcement, actual PrePi/EFI entry path and EL, separate evidence
for non-secure state, final GIC/timer/SMMU ownership, final RAM/carveouts, PSCI
behavior, active DMA masters, and post-exit console are all unknown. These block
an honest Muen implementation milestone. In particular, neither KVM support nor
the CPU's virtualization extension resolves firmware ownership. No matching
public MB1/MB2/EL3 source was inspected, and this report makes no claim about
unavailable manuals or secure-firmware internals.

## Recommended next milestone

Implement only the bounded **EFI handoff/EL2 diagnostic** specified in
[`003-el2-diagnostic-plan.md`](003-el2-diagnostic-plan.md), after its inventory,
authentication, recovery and console prerequisites are met. It must first prove
EFI execution and capture pre-exit state, then optionally perform the guarded
post-exit EL2 observation. It must not initialize GIC/SMMU, start secondaries,
run Muen, launch a subject, or claim isolation.
