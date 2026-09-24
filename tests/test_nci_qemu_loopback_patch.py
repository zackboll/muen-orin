import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "contrib" / "nci-qemu-loopback.patch"
PINNED_NCI = ROOT / "ci" / "nci"


class NciQemuLoopbackPatchTest(unittest.TestCase):
    def test_patch_confines_ssh_forward_to_loopback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            worktree = Path(temporary_directory)
            source = worktree / "steps" / "vm_qemu_xilinx.py"
            source.parent.mkdir()
            source.write_bytes(
                subprocess.run(
                    ["git", "show", "HEAD:steps/vm_qemu_xilinx.py"],
                    cwd=PINNED_NCI,
                    check=True,
                    capture_output=True,
                ).stdout
            )

            subprocess.run(
                ["git", "apply", str(PATCH)],
                cwd=worktree,
                check=True,
            )

            patched = source.read_text(encoding="utf-8")
            self.assertIn("hostfwd=tcp:127.0.0.1:{ssh_port}-:22", patched)
            self.assertNotIn("hostfwd=tcp::{ssh_port}-:22", patched)


if __name__ == "__main__":
    unittest.main()
