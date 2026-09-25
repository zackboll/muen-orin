# Task 002: upstream ARM64 QEMU runtime baseline

Date: 2026-09-23

## Scope and revisions

This report boots and tests the unchanged upstream
`arm64-qemu-zcu102-minimal-debug` system. It does not implement NVIDIA Orin
support, change a source/dependency pin, run GNATprove, or use Orin hardware.

| Item | Value |
|---|---|
| Starting `main` | `18aad9557c189c4ab6d55ba6155688a58343ecd6` |
| Reviewed Task 001 head | `ef53d2fd696211ba5e49041964a0e8d6599d8c36` |
| Task 001 merge | `18aad9557c189c4ab6d55ba6155688a58343ecd6` (PR #1) |
| Historical recipe revision | `084e3143c52956e0323af1911399abdcaff3bfb9` |
| Branch | `feature/002-arm64-qemu-runtime-baseline` |
| NCI | `9ad51d75662d8ad731bae959ba0ed71b821b10d8` |
| NCI configuration | `8ac722ba0551e4958a4d70fe34ab91a1372412e3` |
| Bob layers | `basement` `df866d18`; `basement-gnu-linux` `5e64cdf7`; `ada` `95cb7a0a`; `codelabs` `5909d22d` |

PR #1 was merged before this branch was created. The reviewed Task 001 head is
an ancestor of `origin/main`; the only later commit was its merge commit, with
no content difference. A path-limited diff confirmed that the recipe, launcher,
runner, configuration, and submodule pins at the Task 001 merge are unchanged
from the historical recipe revision. The manifest helper and Task 001 reports,
however, were added after that historical revision.

For reproduction, use a review checkout containing this report and
`contrib/nci-qemu-loopback.patch`. Either run from that integrated checkout after
verifying the recipe-set diff shown below is empty, or create a separate
historical worktree for `084e314...` and carry only the reviewed loopback patch
into its pinned `ci/nci` submodule. Checking out `084e314...` alone does not
provide the manifest helper, Task 001 reports, this report, or the loopback
patch.

## Host environment and prerequisites

The host was Debian 13.7 (`trixie`), kernel `6.12.107+deb13-amd64`, x86-64.
System Python 3.13.5 imported `guestfs` from
`/usr/lib/python3/dist-packages/guestfs.py`. Installed distro packages included:

* `python3-guestfs` and `libguestfs0t64` `1:1.54.1-2+deb13u1`;
* `python3-libvirt` `11.3.0-1`;
* `python3` `3.13.5-1`.

The task created, without replacing an existing directory, the dedicated venv
`/home/zboll/.local/share/muen-orin/task002-venv`:

```sh
/usr/bin/python3 -m venv --system-site-packages \
  /home/zboll/.local/share/muen-orin/task002-venv
```

For every replay shell, activate that environment persistently before running
any command below that invokes Bob, NCI, or `contrib/runQemu.py`:

```sh
export PATH=/home/zboll/.local/share/muen-orin/task002-venv/bin:$PATH
```

The per-command `PATH=...` prefixes retained below are equivalent and remain
part of the historical command record.

The pinned `ci/nci/setup_venv.sh` was inspected but not run because it copies
interpreter-specific extension bindings into the venv. Instead,
`--system-site-packages` exposed the matching distro bindings without copying
them. An exact `pip install -r ci/nci/requirements.txt` stopped at
`libvirt-python==12.3.0`: `libvirt.pc` was unavailable. The exact missing Debian
prerequisite and user-run command are:

```sh
sudo apt install libvirt-dev
```

No system package was installed automatically. The selected Xilinx-QEMU path
does not use libvirt and successfully imported the installed distro binding.
All other pinned requirements were installed exactly by filtering only that
unbuildable line into a temporary requirements file, then installing Bob:

```sh
grep -v '^libvirt-python==' ci/nci/requirements.txt > /tmp/nci-task002.txt
/home/zboll/.local/share/muen-orin/task002-venv/bin/python -m pip install \
  -r /tmp/nci-task002.txt BobBuildTool==1.2.0
rm /tmp/nci-task002.txt
```

The actual launcher/NCI interpreter imported `guestfs`, `libvirt`, YAML, Jinja,
Paramiko, the generic QEMU helper, and the Xilinx QEMU helper. Bob remained
1.2.0. No `user.yaml`, `override_layers.yaml`, `-D` override, sandbox option,
GDB support, or hardware-deployment option was active.

## Loopback-only guest forwarding

The pinned Xilinx helper generated `hostfwd=tcp::<port>-:22`, while the guest
uses documented default credentials. To avoid exposing it beyond the test host,
the minimal reviewed patch `contrib/nci-qemu-loopback.patch` changes only that
forward to `hostfwd=tcp:127.0.0.1:<port>-:22`. It does not alter the guest,
kernel, policy, firmware, devices, firewall, or global networking.

The patch is deliberately separate from the pinned NCI submodule revision:

```sh
/home/zboll/.local/share/muen-orin/task002-venv/bin/python \
  -m unittest tests.test_nci_qemu_loopback_patch -v
git -C ci/nci apply --check ../../contrib/nci-qemu-loopback.patch
git -C ci/nci apply ../../contrib/nci-qemu-loopback.patch
```

Reverse it after testing with
`git -C ci/nci apply -R ../../contrib/nci-qemu-loopback.patch`. Both observed
runtime ports, 4156 for the standalone boot and 40995/55287 for NCI runs 1/2,
listened only on `127.0.0.1`.

## Artifact identities

All paths were obtained with `bob query-path --fail -f '{dist}'`; numbered
workspace paths were not guessed. The selected tools were Xilinx QEMU 8.1.0
(Xilinx 2024.1 recipe) and the matching queried ZCU102 device tree.

| Artifact | Pre-run SHA-256 | Task 001 match |
|---|---|---|
| `kernel` | `015265dcd9920810fffc8d49fad7bb87d89b36749f55ba98606c41f4a44c65b8` | yes |
| `kernel.elf` | `910feb7fb9c2d32fa4ec7856d1300082163303c97359ab6839a533163e16fb90` | yes |
| `BOOT.bin` | `9782b34428d090bf43b08f85d4f18fee1d7c97a1c007cbe509805ccbee21c80c` | yes |
| `sdcard.img` | `7213995246afade2abbefbc3452e8ad5a60eaafd46f8a865fb84f35e1bb58327` | yes |
| `qemu-system-aarch64` | `4b2de9dcb46138c452c802811169d8f3a60fb02a4b7bdf5f594c5fd135dd1eae` | newly recorded |
| `SINGLE_ARCH/zcu102-arm.dtb` | `5ba2bd215e1553e7a7189bdc108a1d0f40bb26c12abac4572aed2d1558718ce0` | newly recorded |

A pristine copy of the original SD image is retained at
`/home/zboll/muen-task002-artifacts/sdcard-pristine.img`. The standalone boot did
not change its hash.

Run 1's required `bob dev` noticed changed Bob build IDs after the repository
advanced to the Task 001 merge, despite the recipe-set content being unchanged.
It performed an incremental build: 41 selected-target packages in 1m31s, zero
checkouts/downloaded packages, with 40 prunes and 80 failed archive attempts
recorded in `bob.log`. The separate x86-named recipe built one
`muen::tools-mulog` analysis package in 0.57s; no x86 guest ran.

The rebuilt raw `kernel` and `BOOT.bin` remained identical. `kernel.elf` changed
to `18f36c263ab9d3aebca283f3027809a3740aff733e52f281ebdf9e052c7c7635`.
Bob pruned the prior ELF before it was copied, so a section-level comparison is
not available. The regenerated SD image changed to
`c72fa4eee7d9f9e26c7189dba1907502b484136ded676e4dee90a5e8975e3bec`.
Exactly 12 image bytes differ: FAT serial/time metadata changed, while the sole
`BOOT.bin` payload is byte-identical. Both NCI runs had equal pre/post SD hashes;
these are build-time differences, not emulator writes.

## Standalone boot

After confirming no existing launcher PID file or QEMU process, the documented
command ran from the repository root:

```sh
PATH=/home/zboll/.local/share/muen-orin/task002-venv/bin:$PATH \
  contrib/runQemu.py -q arm64-qemu-zcu102-minimal-debug
```

The launcher returned 0 and started the queried Xilinx QEMU/DTB. Within 74
seconds, serial 2 showed Linux boot on physical CPU 0 and
`NCI-DHCP_IP: 10.0.2.15`; serial 1 showed repeated
`[-- Subject Running --]` cycles. This is substantive boot evidence rather than
launcher-return evidence. QEMU emitted only the expected warning that hub 0 was
not connected to a host network. `contrib/runQemu.py --terminate-only` then
terminated only its recorded process group and removed its PID file.

## Pinned runtime checks

The selected plan was
`ci/nci-config/arm64/qemu-zcu102-minimal-debug-sdcard.yaml`, rendering
`include/platform-qemu-machine-setup.j2` and
`include/minimal-integration-tests.j2`. It contains one host and eight steps:
QEMU start, DHCP observation, SSH diagnostics, Linux-log expectations,
SSH-output expectations, native-cycle observation, native expectations, and
QEMU stop.

The rendered plan declares 47 runtime conditions: 2 bounded observations, 43
positive expectations, and 2 forbidden-pattern expectations. They cover Linux
boot/model, Muen IRQ chip, architectural timer, clock event/source, GPIO/I2C/
RTC devices, CPU/interrupt diagnostics, native hypercalls, interrupt injection,
timer timestamps, and scheduling timestamps.

```sh
ci/run.sh \
  -a /home/zboll/muen-task002-artifacts/runtime-run1 \
  -r arm64-qemu-zcu102-minimal-debug

ci/run.sh \
  -a /home/zboll/muen-task002-artifacts/runtime-run2 \
  -r arm64-qemu-zcu102-minimal-debug
```

| Result | Run 1 | Run 2 |
|---|---:|---:|
| Plans/hosts | 1/1 | 1/1 |
| Plan steps completed | 8/8 | 8/8 |
| Runtime conditions passed | 47/47 | 47/47 |
| Failed / skipped / blocked | 0 / 0 / 0 | 0 / 0 / 0 |
| NCI summary | `SUCCESS` | `SUCCESS` |
| Runner exit | not retained by initial detached wrapper | 0 |
| Build work | 41 target + 1 mulog package | 0 packages |
| Cleanup | no QEMU/listener/PID file | no QEMU/listener/PID file |

Run 1's missing shell status is an evidence-capture limitation, not represented
as exit 0; its complete NCI terminal summary and all artifacts are retained.
Run 2 closed that gap and confirmed repeatability from a fresh emulator start.

Representative sanitized evidence from run 2:

```text
Booting Linux on physical CPU 0x0000000000 [0x410fd032]
Machine model: Xilinx UltraScale+ ZCU104 - Muen ARM64
arch_timer: cp15 timer(s) running at 100.00MHz (virt).
muen-clkevt: Using timer (event 'muen-clkevt') hwirq 79, virq 79 on CPU#0
muen-clksrc: Initialize clock with 100000 khz
NCI-DHCP_IP: 10.0.2.15
[-- Subject Hypervisor Call 42 --]
[-- Date / Correction (Scheduling) : 2026-09-24T03:44:50 / ... --]
summary - SUCCESS arm64-qemu-zcu102-minimal-debug
```

## Reproduction and evidence locations

From a review checkout, verify recipe equivalence and initialize exact pins:

```sh
git diff --quiet 084e3143c52956e0323af1911399abdcaff3bfb9 -- \
  README.md contrib/runQemu.py ci/run.sh config.yaml default.yaml recipes \
  .gitmodules
git submodule update --init --recursive
PATH=/home/zboll/.local/share/muen-orin/task002-venv/bin:$PATH bob layers update
```

Apply the loopback patch, run the standalone command and cleanup shown above,
then run the single-recipe commands. Do not add `-d`, `-s`, a GDB definition,
or another recipe. Inspect output with, for example:

```sh
grep -aE 'Booting Linux|NCI-DHCP_IP|Subject Running|summary - ' \
  /home/zboll/muen-task002-artifacts/runtime-run2/runner-console.log \
  /home/zboll/muen-task002-artifacts/runtime-run2/arm64-qemu-zcu102-minimal-debug/*/qemu-vm/*.out
```

Complete local evidence is under `/home/zboll/muen-task002-artifacts/`, notably
`standalone/`, `runtime-run1/`, `runtime-run2/`, `pre-run-sha256.txt`,
`plan-counts.txt`, and `sdcard-comparison/`. These logs are intentionally not
tracked because they include large build/runtime output and local paths.

## Limitations and next task

The full pinned Python requirement set remains blocked on the uninstalled
`libvirt-dev` development prerequisite, although the selected runtime path is
validated with the distro libvirt binding. Run 1 did not retain its outer shell
exit code, and the pruned historical `kernel.elf` prevents deeper comparison of
that non-booted debug artifact. Neither limitation affected the repeated pinned
runtime assertions. The broader pinned NCI pytest suite also stops in its
unconditional QCOW2 fixture setup because `virt-builder` is absent; Debian
provides it with `sudo apt install guestfs-tools`. No package was installed
automatically. GitHub exact-head results are recorded in the PR review rather
than claimed here before the final commit exists.

The recommended next task is a read-only Tegra234 platform-contract study that
defines firmware/EL2 handoff, GICv3, SMMU, timer, and console ownership before
any Orin kernel, policy, firmware, or device-assignment implementation. GNATprove
and Orin hardware testing remain separate and were not run.
