# Task 001: upstream ARM64 baseline

Date: 2026-09-23

## Scope and starting state

This investigation reproduces the existing upstream ZynqMP/QEMU ARM64 target. It
does **not** port Muen to NVIDIA Orin and did not flash or otherwise access target
hardware.

| Item | Value |
|---|---|
| Repository | `git@github.com:zackboll/muen-orin.git` |
| Upstream | `https://github.com/codelabs-ch/bob-muen.git` |
| Branch | `feature/001-arm64-baseline` |
| Starting/base SHA | `084e3143c52956e0323af1911399abdcaff3bfb9` |
| Initial tree | clean; branch already existed at `origin/main` |
| Submodules | `ci/nci` at `9ad51d75662d8ad731bae959ba0ed71b821b10d8`; `ci/nci-config` at `8ac722ba0551e4958a4d70fe34ab91a1372412e3` |

No reset, clean, stash, rebase, amend, force-push, global configuration change,
system toolchain replacement, or security-setting change was performed.

## Host and tools

| Item | Observed value |
|---|---|
| OS | Debian GNU/Linux 13.7 (trixie) |
| Kernel | `6.12.107+deb13-amd64` |
| Host architecture | `x86_64`, 32 logical CPUs |
| Memory | 89 GiB total, 62 GiB available at preflight |
| Disk | 1.8 TiB filesystem, 942 GiB available at preflight |
| Python | 3.13.5 |
| Git | 2.47.3 |
| GitHub CLI | 2.46.0 |
| Bob | 1.2.0, isolated in `/tmp/muen-task001-venv` |
| Host GNAT | 14.2.0 (not selected by the recipe) |
| Host GNATprove | FSF 16.1.0 (not selected or run) |
| Bob AArch64 GCC/GNAT | 13.4.0 |
| Bob Xilinx QEMU | 8.1.0 (Xilinx 2024.1 recipe) |

Bob was installed according to its official documentation with
`python3 -m venv /tmp/muen-task001-venv` and
`python -m pip install BobBuildTool==1.2.0`. An initial attempt with the incorrect,
case-insensitive-looking name `bob-build-tool` failed because no such PyPI
distribution exists; it made no repository or system change. The build obtains
its own pinned compiler/tool packages through Bob.

User namespaces were available (`kernel.unprivileged_userns_clone=1`, and a
test `unshare --user --map-root-user true` exited 0). There was no `user.yaml`,
`override_layers.yaml`, or `-D` override. The exact README command had no
sandbox option, and `bob show --format json -f sandbox
arm64-qemu-zcu102-minimal-debug` returned `{}` for the same configuration. Thus
this was an unsandboxed Bob development build, although sandbox-toolchain
packages remain dependencies in the graph. `ci/run.sh` can independently request
sandbox mode with `-s`; that path was not used.

## Target and dependency path

`arm64-qemu-zcu102-minimal-debug` is the smallest documented ARM64 demo. It is
not only a kernel build. Its direct graph includes:

* the Muen ARM64 kernel and zero-footprint runtime;
* policy composition, validation, and code/page-table/device-tree generators;
* prebuilt ZynqMP FSBL, PMU firmware, and bitstream from the ARM64 systems repo;
* a Muen Linux 6.1.82 subject, Muen Linux modules, and a demo initramfs;
* debug server, time, and event-logger subjects;
* an AArch64 GNU/Ada cross-toolchain and host GNAT/tool generators;
* Xilinx Bootgen, Xilinx QEMU 2024.1, and Xilinx QEMU device trees.

The actual separation-kernel source is
`https://git.codelabs.ch/muen/arm64/kernel.git` at
`bb48b4c5ec7d33aba58edc2fc89d4d2996ca568f`; it is not contained in this
top-level recipes repository. The complete resolved source summary is in
[`001-source-manifest.json`](001-source-manifest.json).

## Commands and results

All commands were run from the repository root with
`PATH=/tmp/muen-task001-venv/bin:$PATH` where Bob was needed.

| Command | Exit | Result |
|---|---:|---|
| `git submodule update --init --recursive` | 0 | Both declared submodules checked out at superproject-pinned SHAs. |
| `bob layers update` | 0 | Four layers fetched at configured commits. |
| `bob ls` | 0 | Requested target listed exactly. |
| `bob query-scm -r ... arm64-qemu-zcu102-minimal-debug` | 0 | 177 SCM entries: 27 Git and 150 URL entries, preserving SHA-1/SHA-256/SHA-512. |
| `bob dev arm64-qemu-zcu102-minimal-debug` | 0 | Passed in 10m24s: 5 checkouts, 41 packages built, 67 downloaded. |
| `contrib/runQemu.py -q arm64-qemu-zcu102-minimal-debug` | 1 | Blocked before QEMU startup: missing NCI `guestfs` Python/system dependency. |

Full local logs were copied from `/tmp` to the deliberately untracked evidence
directory before corrective work:

* `/home/zboll/muen-task001-evidence/muen-task001-bob-install-corrected.log`
* `/home/zboll/muen-task001-evidence/muen-task001-bob-layers-update.log`
* `/home/zboll/muen-task001-evidence/muen-task001-bob-ls.log`
* `/home/zboll/muen-task001-evidence/scm-all-digests.txt`
* `/home/zboll/muen-task001-evidence/muen-task001-bob-dev.log`
* `/home/zboll/muen-task001-evidence/muen-task001-runQemu.log`
* `/home/zboll/muen-task001-evidence/muen-task001-artifacts.log`

Representative layer evidence:

```text
** CHECKOUT: Layer 'basement' .. ok
** CHECKOUT: Layer 'basement-gnu-linux' .. ok
** CHECKOUT: Layer 'ada' .. ok
** CHECKOUT: Layer 'codelabs' .. ok
bob layers update exit=0
```

Representative target evidence:

```text
arm64-qemu-zcu102-minimal-debug
bob ls exit=0
target grep exit=0
```

Representative build evidence:

```text
Build result is in dev/dist/arm64-qemu-zcu102-minimal-debug/1/workspace
Duration: 0:10:23.940727, 5 checkouts (0 overrides active),
41 packages built, 67 downloaded.
```

Key artifact SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| `kernel` | `015265dcd9920810fffc8d49fad7bb87d89b36749f55ba98606c41f4a44c65b8` |
| `kernel.elf` | `910feb7fb9c2d32fa4ec7856d1300082163303c97359ab6839a533163e16fb90` |
| `BOOT.bin` | `9782b34428d090bf43b08f85d4f18fee1d7c97a1c007cbe509805ccbee21c80c` |
| `sdcard.img` | `7213995246afade2abbefbc3452e8ad5a60eaafd46f8a865fb84f35e1bb58327` |
| Bob audit | `e050fb2a9665ee405fcb496db171923df1d5df8a505e9e3bc14ec461c02be055` |

## Build, boot, test, and proof status

These outcomes are intentionally separate:

* **Build:** **passed**. The final package contains `kernel`, `kernel.elf`,
  `BOOT.bin`, `sdcard.img`, the Linux image/DTB/initramfs, subject binaries, and
  generated policy artifacts. Binary archive misses correctly fell back to
  local builds and were not failures. This is build-time validation only; it
  does not validate runtime behavior of the kernel, GIC, timer, SMMU, scheduler,
  subjects, or Linux.
* **Boot:** **blocked before execution**. The launcher imports the x86 `VmQemu`
  helper unconditionally; that imports Python `guestfs`. The initialized NCI
  submodule's `setup_venv.sh` requires system package `python3-guestfs`, which
  was absent. Installing it requires a system package change, so it was not
  installed automatically. Both launcher attempts exited before QEMU startup;
  no launcher PID file, QEMU process, or serial output was created. The first
  exposed missing pinned `colorlog==6.10.1`, which was safely added to the
  isolated task venv; the second preserved the actionable `guestfs` blocker.
* **Tests:** **not run / blocked**. The debug recipe or launcher does not itself
  execute the NCI assertions. The authoritative NCI plan
  (`ci/nci-config/arm64/qemu-zcu102-minimal-debug-sdcard.yaml`) additionally
  requires Linux boot, SSH diagnostics, Muen IRQ/timer/clocksource devices, and
  a complete native-subject hypercall/interrupt/scheduling cycle. Those checks
  are the success criteria intended for serial assessment.
* **Proof:** **not run**. Proof is a separate `*-proof` recipe; a debug build or
  Markdown lint result must not be reported as proof evidence.

## Source inventory and reproducibility limits

The top-level SHA alone is insufficient. To reselect the observed source set:

1. Install exactly `BobBuildTool==1.2.0` in an isolated environment.
2. Check out the top-level SHA and initialize its two pinned submodules.
3. Run `bob layers update`; verify the four layer SHAs from the manifest.
4. Ensure no user/override files or `-D` definitions are active and preserve the
   recorded unsandboxed effective configuration.
5. Run the exact query and transformation commands recorded in the manifest and
   compare all 177 ordered records.

All four layers and 26 of 27 recursive Git SCMs specify commits. The remaining
Git SCM is tag-only (`sbsigntools` `v0.9.5`). Of 150 URL records, 98 configure
SHA-256 or SHA-512 (97 SHA-256 and one SHA-512), and 52 configure SHA-1 only.
No URL record lacks all three supported digest fields, and none has multiple
configured digests. SHA-1-only selection is weaker than SHA-256/SHA-512 and the
tag-only Git selection is not commit-addressed.

The manifest distinguishes recipe configuration from build observation. The
retained source audits directly record five clean Git checkouts at configured
commits: kernel, systems, tools, components, and component-libs. The final audit
contains 1,033 dependency references plus build/result identifiers. Many
packages were restored from binary archives; their audit chains are provenance
metadata, not evidence that this host checked out and independently hashed each
source. No direct retained checkout/audit records the tag-only Git source's
commit, so it remains unavailable rather than being reconstructed from the
current remote tag. No bit-for-bit rebuild comparison was made, and this report
does not claim a fully verified source closure.

Source overrides already exist through ignored local files (`user.yaml` and
`override_layers.yaml`), Bob `-D` definitions, and recipe variables such as
`MUEN_COMMON_{URL,BRANCH,COMMIT}` and `MUEN_TOOLS_{URL,BRANCH,COMMIT}`. No
override was used here.

## GitHub CI evidence

The tracked `.github/workflows/system_images.yml` is push-triggered and includes
`arm64-qemu-zcu102-minimal-debug` in its build matrix. For reviewed head
`0ea529e3552caeb958818ec0f08f55ee9590c012`, separate GitHub API queries returned:

* zero check runs;
* zero commit-status contexts; and
* zero Actions workflow runs with that head SHA and `push` event.

Additionally, `gh workflow list --all` returned no registered workflows. These
are observations, not proof that workflows are absent from the repository, and
no cause for the missing run was established. Local pre-commit results are
lint/format validation only and are not system-image, runtime, or proof CI.

## References

* Repository `README.md`, `config.yaml`, `default.yaml`, `recipes/arm64.yaml`
* `recipes/muen/arm64-kernel.yaml`, `arm64-systems.yaml`, `linux.yaml`
* `contrib/runQemu.py`, `ci/run.sh`, and the pinned NCI ARM64 plan/templates
* Bob installation documentation: <https://bob-build-tool.readthedocs.io/en/latest/installation.html>
* Compatibility evidence: [`orin-gap-analysis.md`](orin-gap-analysis.md)
