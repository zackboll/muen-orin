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

1. `PrePi/AArch64/ModuleEntryPoint.S` is a relocatable ELF PIE loaded in RAM.
   On EL3 entry it programs `SPSR_EL3` from `PcdArmNonSecModeTransition`,
   `SCR_EL3` from `PcdArmScr`, `CNTFRQ_EL0` from
   `PcdTegraArchTimerFreqInHz`, enables the GICv3 system-register interface,
   makes SGI/PPI/SPI interrupts non-secure Group 1, enables affinity routing,
   and `eret`s. The matching AArch64 EDK2 defaults are SPSR `0x3c9` (EL2h with
   DAIF masked) and SCR `0x501` [NVIDIA-EDK2-R3644,
   NVIDIA-EDK2-BASE-R3644]. This is strong source evidence that this build runs
   non-secure UEFI at EL2; it is not evidence for an unknown installed build.
2. PrePi obtains the UEFI carveout and boot parameters from the preceding CPU
   bootloader, constructs the memory/resource HOBs, reserves firmware and DTB,
   enables its own MMU, registers the firmware DT, and enters DXE. Its T234
   resource code carries forward firmware carveouts and VPR rather than
   treating all DRAM as free [NVIDIA-EDK2-R3644].
3. NVIDIA documents UEFI as the rel-34-and-later bootloader and supports normal
   UEFI boot options. The r36.4.4 L4T launcher parses `extlinux.conf`, applies
   DT overlays, uses UEFI `LoadImage`, supplies load options, then calls
   `StartImage` [NVIDIA-UEFI-R3644, NVIDIA-EDK2-R3644]. Thus a PE/COFF AArch64
   EFI application on an enabled boot device is the best-supported ingestion
   format. A raw Muen ELF is not established as an accepted firmware image.
4. UEFI application entry receives `ImageHandle` and `SystemTable`. It is still
   a firmware client: boot services and the firmware's mappings/state remain in
   force. A loader must obtain the final map and matching key immediately before
   successful `ExitBootServices`; afterward it may reclaim only types permitted
   by UEFI and must preserve runtime/reserved regions [UEFI-2.10]. EFI entry is
   therefore not synonymous with a post-firmware Muen entry.

### Best-supported route

The candidate route is **NVIDIA's existing chain -> non-secure UEFI -> a small
AArch64 EFI loader/probe -> `ExitBootServices` -> a separately placed EL2
diagnostic payload**. The EFI stage must allocate pages through UEFI with an
explicit physical-placement policy, copy the payload, snapshot the final UEFI
map and firmware DT, stop using boot services, then branch only if `CurrentEL`
is EL2. Payload alignment is its own linker/loader contract; neither a Muen ELF
load address nor the generic Linux `Image` placement rule is silently reused.

Entry arguments to the EFI image are the UEFI pair, not Linux's `x0 = DTB` and
not Muen's current no-argument entry. The proposed private payload ABI is:

* `x0`: physical address of a page-aligned, loader-owned handoff structure;
* `x1`-`x3`: zero;
* structure: size/version, copied final UEFI memory map and descriptor metadata,
  firmware DT physical address/size, EFI-stage observations, payload range,
  selected output method, and checksum;
* one boot CPU only; secondaries remain under firmware/PSCI control;
* DAIF masked; payload installs `VBAR_EL2` before any deliberate exception;
* payload records incoming `SCTLR_EL2`, `TCR_EL2`, `TTBR0_EL2`, `MAIR_EL2`,
  `HCR_EL2`, `CNTHCTL_EL2`, `CNTVOFF_EL2`, `ICC_SRE_EL2`, `MPIDR_EL1`,
  `CNTFRQ_EL0`, and feature ID registers before changing them.

Linux's documented AArch64 entry convention is useful comparison evidence but
is not adopted merely because L4T boots Linux [LINUX-AARCH64-BOOT].

The loader must perform architecturally required cache maintenance and barriers
for copied executable code before transfer. Whether it keeps UEFI's EL2 stage-1
tables briefly or disables the MMU is an implementation choice gated by the
captured attributes and Arm rules; the current Muen startup explicitly requires
its incoming EL2 MMU disabled [MUEN-KERNEL]. The first diagnostic should not
jump into Muen.

### Conditions not yet established

The route is a candidate, not yet a supported target procedure, until all of
these are known: actual board identity; installed UEFI version; whether its boot
manager exposes a non-destructive removable-media/boot-option path; Secure Boot
state and an authorized way to authenticate the EFI image; actual EFI entry EL;
successful `ExitBootServices`; final memory map/DT; and a physically routed,
electrically safe console. No evidence here authorizes changing keys, fuses,
firmware, or boot partitions.

## Ownership and initialization contract

| Area | Current Muen assumption | r36.4.4/T234 evidence | Proposed EL2 responsibility | Blocking unknown |
|---|---|---|---|---|
| GICv3 | Muen instantiates GIC-400/GICv2, requires all interrupts disabled/non-secure, validates a GIC-400 IIDR, and uses GICC/GICH/GICV MMIO [MUEN-KERNEL]. | PrePi enables ICC system registers, marks SGI/PPI/SPI Group 1 non-secure and enables GICv3 affinity routing. Linux describes a GICv3 distributor/redistributor; NVIDIA UEFI discovers these from DT [NVIDIA-EDK2-R3644, LINUX-V6.1-T234, ARM-GIC]. | Diagnostic is read-only: record `ICC_SRE_EL2`, GIC ID registers only where safe/authorized, DAIF, and DT ranges. Do not disable/re-group/re-route interrupts. A later port needs a GICv3 CPU + virtualization interface and per-CPU redistributor init. | Installed firmware's final groups, enables, routes, maintenance PPI, hypervisor timer PPI and EL3 ownership. Blocks interrupts and Muen scheduling. |
| Timer | Reads `CNTFRQ_EL0`, programs CNTHP at EL2, changes `CNTHCTL_EL2`, and assumes policy IRQ constants/routing [MUEN-KERNEL]. | UEFI source writes 31.25 MHz to `CNTFRQ_EL0` before EL2 and later permits EL1 physical-counter/timer access. Linux describes the architectural timer; these do not guarantee the final trap/routing state [NVIDIA-EDK2-R3644, LINUX-V6.1-T234]. | Treat `CNTFRQ_EL0` as source of truth; record counter progress, CNTHCTL/HCR/CNTVOFF and DT PPIs. Do not enable a timer interrupt in milestone 1. | Final timer PPI group/priority/route and secure firmware trapping. Blocks timer IRQ and scheduler tick, not a polled counter check. |
| CPU/PSCI | Direct EL2 startup requires core 0 and every policy CPU already running; affinity extraction is ZynqMP-oriented [MUEN-KERNEL]. | T234 DT advertises PSCI 1.0 and MPIDRs; UEFI derives enabled-core records from bootloader/fuse data [LINUX-V6.1-T234, NVIDIA-EDK2-R3644]. | Use only the boot CPU; record MPIDR and PSCI DT node/conduit. Make no `CPU_ON` call and do not assume MPIDR 0. Preserve firmware PSCI for later secondary release [ARM-PSCI]. | Actual primary affinity, enabled six-core SKU map, conduit and post-exit PSCI functionality. Blocks multicore only. |
| RAM/translation | Muen expects MMU off, installs EL2 stage-1 and guest stage-2 tables from static policy, and assumes a static ZynqMP map [MUEN-KERNEL, MUEN-SYSTEMS]. | PrePi enables EL2 stage-1, builds resources from bootloader-provided DRAM/carveouts, preserves UEFI/DT/VPR and other carveouts. UEFI defines final-map ownership after exit [NVIDIA-EDK2-R3644, UEFI-2.10]. | Allocate loader/payload pages via UEFI; preserve all final-map reserved/runtime regions and DT reserved-memory; inspect feature registers; perform cache/TLB maintenance before any translation change. Stage-2 remains unused in experiment 1. | Actual 8GB map, carveouts, PA/granule/stage-2 features and firmware runtime dependencies. Blocks Muen policy and translations. |
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

* The selected r36.4.4 source set reaches NVIDIA UEFI through the documented
  T23x chain and its matching source configures non-secure EL2 before UEFI.
* UEFI can load/start AArch64 EFI images; this establishes an ingestion path,
  not automatic acceptance under unknown Secure Boot policy.
* Firmware-derived memory and carveout data must survive into the handoff.
* Tegra234 uses GICv3 and Linux implementation data describes three SMMUv2/
  SMMU-500-compatible controllers; Muen implements GICv2 and one SMMU model.
* The architectural counter frequency is firmware-programmed in this source
  set; the payload must read it rather than hard-code it.
* TCU is mailbox based and distinct from a directly programmed 16550 UART;
  physical console routing remains carrier-specific.

## Critical unresolved assumptions

Installed release/security policy, board/carrier identity, EFI acceptance,
actual EFI entry EL, final GIC/timer/SMMU ownership, final RAM/carveouts, PSCI
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
