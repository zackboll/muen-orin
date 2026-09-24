# Initial NVIDIA Orin compatibility gap analysis

This analysis compares the resolved upstream ARM64 baseline with NVIDIA
Tegra234, with **Jetson Orin Nano 8GB** as the intended starting module. It is
not a compatibility claim or a port design. No Orin hardware was accessed,
flashed, or tested. An exact carrier-board revision and firmware release have
not been selected.

## Evidence conventions

* **Inspected implementation** means code at the exact revisions below was read.
* **Documented behavior** means upstream Muen, NVIDIA, or Linux documentation/
  hardware-description source states the behavior.
* **Build-time validation** means the ZynqMP image assembled successfully; it
  does not establish subsystem behavior.
* **Runtime result** is blocked before QEMU startup, so no subsystem runtime
  validation occurred.
* **Unknown** identifies information that must not be inferred from ARM64 alone.

Primary Muen sources are the ARM64 kernel
`bb48b4c5ec7d33aba58edc2fc89d4d2996ca568f` and systems repository
`99f9e6264ef11af6ba7db66abedaa1b058683f33`. Orin comparison sources are the
NVIDIA Jetson Linux r36.4.4 documentation and upstream Linux v6.1
`arch/arm64/boot/dts/nvidia/tegra234*.dts*`. Tegra234-wide controller/CPU facts
are separated below from module/carrier connector and console routing. AGX Orin
developer-kit material is retained only where explicitly labeled reference data;
it is not evidence for the intended Orin Nano carrier.

NVIDIA designates the Orin Nano 8GB CPU as six-core Arm Cortex-A78AE. Upstream
Linux v6.1 uses the literal device-tree compatible string `"arm,cortex-a78"` for
Tegra234 CPU nodes; preserving that literal does not change NVIDIA's product CPU
designation. NVIDIA's Orin Nano development example uses a P3767 SOM on a P3768
reference carrier (P3766 kit), but this task does not assume that exact carrier
revision for the intended hardware.

## Compatibility matrix

### Boot entry contract and exception level — incompatible/unknown

* **Inspected implementation:** `src/main/startup.S:25-71` accepts EL3 or EL2.
  EL3 enters the bundled ZynqMP-oriented BL31 code. Direct EL2 requires core 0
  and all policy cores already running, EL2 access enabled through `ACTLR_EL3`,
  MMU off, GIC-400 interrupts disabled/routed non-secure, and SMMU-500 resources
  assigned to non-secure software. Other exception levels shut down. The image
  is compiled with `-mcpu=cortex-a53` (`kernel.gpr:121-129`). The systems boot
  image includes Xilinx FSBL, PMU firmware, and a programmable-logic bitstream
  (`deployment/common/bootgen/bootgen.bif`).
* **Documented behavior:** NVIDIA's Jetson boot architecture identifies a T23x
  chain involving BootROM, PSCROM, MB1, MB2, and UEFI. NVIDIA's platform guide
  requires T234 MB1 BCT and device-tree adaptation. Linux describes Tegra234 CPU
  nodes with literal compatible `"arm,cortex-a78"` and PSCI enablement; the
  intended Orin Nano 8GB module is a six-core Cortex-A78AE product.
* **Build/runtime evidence:** the ZynqMP image built. QEMU did not start because
  of the host `guestfs` prerequisite; no boot contract was exercised. No Orin
  runtime test occurred.
* **Unknown:** the supported NVIDIA firmware handoff to a custom non-secure EL2
  payload, required secure-firmware changes, CPU release state, reserved-memory
  contract, and ownership/security state of GIC/SMMUs. These require NVIDIA's
  T234 TRM and firmware/secure-boot documentation before implementation.

### Interrupt controller — implementation gap

* **Inspected implementation:** the kernel instantiates `GIC.GIC400` and uses
  GICv2 distributor, CPU-interface, and virtual-interface-control MMIO. It
  rejects hardware unless `GICD_IIDR` implementer is `0x43b` and product is
  `0x02` (`src/main/arm/gic/gic-gic400.adb:45-75`). ZynqMP policy maps GICD,
  GICC, GICH, and GICV at `0xf9010000` through `0xf9070000`
  (`policy/hardware/xilinx-zcu104.xml:21-54`).
* **Documented behavior:** the kernel README explicitly supports GICv2 only
  (`README.md:26-29`). Linux v6.1 describes Tegra234's main controller as
  `arm,gic-v3`, with GICD at `0x0f400000` and GICR at `0x0f440000`; that is a
  different programming and virtualization model with redistributors/system
  registers rather than the required GICC/GICH/GICV layout.
* **Build/runtime evidence:** policy generation and image build accepted the
  configured ZynqMP GICv2 model. QEMU did not start, so neither GICv2 behavior
  nor the CI serial expectations ran; no GICv3 or Orin test occurred.
* **Unknown:** exact GICv3 revision/features, ITS requirements, interrupt routing,
  and firmware ownership on the target module. A new GICv3 implementation and
  proof strategy are required; Linux guest `CONFIG_ARM_GIC_V3` does not provide
  a host-kernel implementation.

### Timer and scheduling — architecture overlap, platform validation required

* **Inspected implementation:** each core initializes ARM generic timer state;
  scheduling programs the EL2 physical timer against absolute physical-counter
  deadlines (`src/main/arm/cpu/timer/armv8-generic_timer.adb:30-62`,
  `src/main/sk/sk-scheduler.adb:856-877`). ZynqMP policy fixes the frequency at
  100 MHz, explicitly matching FSBL configuration
  (`policy/hardware/xilinx-zcu104.xml:3-13`).
* **Documented behavior:** Linux v6.1 describes Tegra234 with an
  `arm,armv8-timer` node and the standard secure/non-secure/virtual/hypervisor
  PPIs. This shows architectural timer presence, not Muen timing correctness.
* **Build/runtime evidence:** image build only; QEMU did not start, so there was
  no timer runtime validation or Orin measurement.
* **Unknown:** CNTFRQ consistency, timer access/trap state at handoff, PPI routing,
  counter synchronization, and worst-case scheduling behavior on Cortex-A78.
  Policy frequency and scheduling assumptions must be measured and validated.

### Stage-2 translation and TLB maintenance — reusable architecture, CPU gap

* **Inspected implementation:** the Cortex-A53-specific SLAT layer accesses
  `VTTBR_EL2` and `VTCR_EL2`; startup establishes EL2 stage-1 state and clears
  EL2 TLB/cache (`src/main/arm/cpu/cortex-a53/armv8-cortex_a53-slat.adb`,
  `src/main/startup.S:160-221`). Subject switches install generated stage-2
  state. Low-level register access is `SPARK_Mode => Off`.
* **Documented behavior:** Tegra234 Linux CPU nodes are Cortex-A78 with PSCI,
  while the Muen project compiles for Cortex-A53. Both implement ARMv8-A
  virtualization, but that does not validate feature-register assumptions,
  cache maintenance, TLB invalidation scopes, barriers, VMID width, or errata.
* **Build/runtime evidence:** Cortex-A53 code compiled; it did not run. No
  Cortex-A78AE/Orin test occurred.
* **Unknown:** Orin cache topology and coherency requirements, supported
  translation granules/IPA size/VMID width, FEAT_* behavior, and applicable
  Cortex-A78/Tegra errata. Audit against the target ID registers and manuals.

### SMMU and DMA isolation — major platform-policy gap

* **Inspected implementation:** `SMMU.SMMU500` programs SMMUv2 stream matching,
  stage-2 context banks, faults, and TLB synchronization. ZynqMP policy assumes
  one controller at `0xfd800000`, 48 stream mappings, 16 context banks, 39-bit
  address width, and ZynqMP stream IDs (`policy/hardware/xilinx-zcu104.xml:56-67,
  107-150`). Kernel initialization treats SMMU failure as fatal unless GDB mode
  deliberately excludes its EL3 setup.
* **Documented behavior:** the Muen README supports SMMUv2 only. Linux v6.1
  describes three Tegra234 `nvidia,tegra234-smmu`/`nvidia,smmu-500` instances
  (`smmu_iso`, `smmu_niso0`, `smmu_niso1`) at different addresses, and devices
  use Tegra234-specific stream IDs.
* **Build/runtime evidence:** ZynqMP SMMU policy generation/build passed, but no
  SMMU code ran; no Orin DMA or fault-containment experiment occurred.
* **Unknown:** secure/non-secure ownership, bypass/default-domain state, context
  bank availability, SID routing, GPU and firmware-managed DMA, ATS/PCIe details,
  and whether all relevant masters can be isolated. Do not assign GPU/CUDA or
  other DMA devices until this is established.

### Multicore initialization and synchronization — implementation gap

* **Inspected implementation:** startup expects required cores already running,
  derives IDs from MPIDR, idles cores outside policy, and uses global barriers.
  BSP core 0 initializes common state, then releases peers; all cores synchronize
  before starting preemption timers (`src/main/startup.S:32-46,87-120` and
  `src/main/sk/sk-kernel.adb:1082-1108,1273-1293`).
* **Documented behavior:** current ZynqMP policy is four Cortex-A53 cores. Linux
  v6.1 Tegra234 CPU nodes use literal compatible `"arm,cortex-a78"` and PSCI;
  NVIDIA's intended Orin Nano 8GB module designation is six-core Cortex-A78AE.
* **Build/runtime evidence:** the selected *minimal* baseline is one-core and
  built only; QEMU did not start. It does not test the separate multicore recipe,
  and no Orin test occurred.
* **Unknown:** CPU count/SKU policy, PSCI versus spin-table/custom release path,
  affinity mapping, coherency enablement, power management, and reset/offline
  behavior under NVIDIA firmware.

### Debug console and platform configuration — replacement required

* **Inspected implementation:** kernel diagnostics map ZynqMP UART0 at
  `0xff000000`; the debug server maps UART1 at `0xff010000`. The implementation
  uses Xilinx PS UART and waits on its TX FIFO during handoff
  (`policy/platform/xilinx-zcu104.xml`, `policy/hardware/xilinx-zcu104.xml:69-81`,
  and `src/main/sk/sk-kernel.adb:1089-1093`). GPIO, I2C, GEM, USB, RTC, RAM, and
  interrupt mappings are all ZynqMP-specific.
* **Documented behavior:** Tegra234-wide Linux source defines a mailbox-backed
  Tegra Combined UART (`nvidia,tegra234-tcu`) and UARTA
  (`nvidia,tegra234-uart` at `0x03100000`). The AGX Orin developer-kit source
  aliases `serial0` to TCU and its guide identifies AGX-specific J26/J502 debug
  connectors; those connector facts are reference data, not Orin Nano routing.
  The Orin Nano developer-kit guide instead documents USB-C device mode exposing
  USB serial access. Actual routing depends on the selected module/carrier and
  board configuration.
* **Build/runtime evidence:** ZynqMP console code built; no console ran and no
  Orin console test occurred.
* **Unknown:** preferred early-console path for an EL2 payload, TCU mailbox
  firmware dependencies, clock/reset/pinmux state, and whether UARTA is safely
  available. A new hardware/platform policy and console driver are required.

### Linux-subject support — substantial adaptation required

* **Inspected implementation:** the target uses the pinned Muen Linux fork
  `0981bbbe3076c1a3282bcf900f20cf6937b8cb3c`, generated DTBs, and a policy that
  maps a Linux image at virtual `0x00200000`. The minimal guest gets Muen
  paravirtual console, IRQ chip, clock event/source, information, events, and a
  ZynqMP GEM device domain (`policy/system/minimal.xml`). CI expects the model
  string `Xilinx UltraScale+ ZCU104 - Muen ARM64` and ZynqMP GPIO/I2C devices.
* **Documented behavior:** NVIDIA's L4T platform guide relies on T234 BCT,
  device trees, firmware, BPMP-managed clocks/resets, and board-specific drivers.
  Upstream Tegra234 DTS has a materially different device graph and IOMMU SIDs.
* **Build/runtime evidence:** the existing ZynqMP QEMU image built, but QEMU did
  not start because of the host launcher prerequisite. No Linux subject boot,
  Orin guest, NVIDIA driver, GPU, or CUDA test occurred.
* **Unknown:** which upstream/L4T kernel can run as a Muen subject, required
  hypercalls/paravirtual drivers, firmware carveouts and mailbox services,
  device assignment, initramfs userspace, and NVIDIA proprietary component
  assumptions. GPU/CUDA enablement is explicitly outside this task.

### SPARK/proof boundary — partial and unchanged

* **Inspected implementation:** the separate proof recipe invokes GNATprove on
  the Ada `sk-kernel` main in `BUILD_MODE=prove`. A lexical inventory at this
  revision finds 78 Ada source files, ten assembly files, and 19 Ada files that
  contain `SPARK_Mode => Off`. This count is not a complete measurement of proof
  coverage or trusted code. The matching files include system-register access,
  CPU/FPU/MMU/SLAT/timer operations, SMMU barriers, atomics, locks, barriers,
  power, and architecture glue. Startup, EL3 GIC/SMMU setup, exception vectors,
  cache code, and subject entry are assembly and are not Ada proof units.
* **Documented behavior:** `classes/muen_proof.yaml` uses GNATprove
  `--mode=all --proof=progressive`; `kernel.gpr` selects release sources for
  proof. These settings describe intended analysis; they do not establish that
  any obligation passed in this task or prove hardware/assembly correctness.
* **Proof result:** GNATprove was not run. There are no proof results for Task
  001, and the debug build provides none.
* **Unknown:** proof obligations and trusted-code growth for future GICv3,
  Cortex-A78AE, boot, console, SMMU-topology, and policy changes. A port must define
  these boundaries before implementation, not retroactively label tests proof.

## Authoritative comparison references

* NVIDIA Jetson Linux r36.4.4, Boot Architecture:
  <https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/AR/BootArchitecture.html>
* NVIDIA Jetson AGX Orin Platform Adaptation and Bring-Up:
  <https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/HR/JetsonModuleAdaptationAndBringUp/JetsonAgxOrinSeries.html>
* NVIDIA Jetson AGX Orin Developer Kit hardware layout:
  <https://developer.nvidia.com/embedded/learn/jetson-agx-orin-devkit-user-guide/developer_kit_layout.html>
  (explicitly AGX reference data only)
* NVIDIA Jetson Orin NX and Nano module/carrier adaptation guide:
  <https://docs.nvidia.com/jetson/archives/r36.4.4/DeveloperGuide/HR/JetsonModuleAdaptationAndBringUp/JetsonOrinNxNanoSeries.html>
* NVIDIA Jetson Orin Nano Developer Kit hardware layout:
  <https://developer.nvidia.com/embedded/learn/jetson-orin-nano-devkit-user-guide/hardware_spec.html>
* Linux v6.1 Tegra234 hardware description:
  <https://github.com/torvalds/linux/blob/v6.1/arch/arm64/boot/dts/nvidia/tegra234.dtsi>
* Linux v6.1 developer-kit description:
  <https://github.com/torvalds/linux/blob/v6.1/arch/arm64/boot/dts/nvidia/tegra234-p3737-0000+p3701-0000.dts>

## Conclusion

ARM64 ISA commonality is insufficient. The generic timer and broad stage-2
architecture offer potential reuse, but boot/firmware handoff, Cortex-A78AE CPU
setup, GICv3, multicore PSCI flow, SMMU topology and stream IDs, console, policy,
and Linux device/firmware integration are concrete gaps. The next task should be
a read-only T234 platform-contract study that obtains authoritative register and
firmware ownership information and defines a minimal EL2 handoff—before any
kernel implementation or hardware flashing.
