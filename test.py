import os
import random
import string
import subprocess
import unittest
import itertools
from pathlib import Path

"""
Advanced test‑suite for CS 111 Lab 2 (pipe.c).
Run with:  python -m unittest test_pipe_advanced.py
Assumes:
    * make / make clean build targets exist (same as basic tests)
    * System is Linux‑like (uses /proc and SIGPIPE semantics)
"""


class TestPipeAdvanced(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = subprocess.run(['make'], capture_output=True, text=True)
        assert build.returncode == 0, f"make failed:\n{build.stdout}\n{build.stderr}"

    @classmethod
    def tearDownClass(cls):
        subprocess.run(['make', 'clean'], capture_output=True)

    # --------------- helpers --------------- #
    def _run_pipe(self, *cmds, input_data=b""):
        """Run ./pipe with *cmds and return (stdout, stderr, rc)."""
        res = subprocess.run(['./pipe', *cmds], input=input_data, capture_output=True)
        return res.stdout, res.stderr, res.returncode

    def _run_shell(self, pipeline, input_data=b""):
        """Run a shell pipeline string and return its stdout."""
        res = subprocess.run(pipeline, input=input_data, capture_output=True, shell=True)
        return res.stdout

    # --------------- positive functionality --------------- #
    def test_single_command_passthrough(self):
        payload = b"hello\nworld\n"
        out_pipe, _, rc = self._run_pipe('cat', input_data=payload)
        self.assertEqual(rc, 0)
        self.assertEqual(out_pipe, payload)

    def test_long_pipeline_eight_processes(self):
        text = "\n".join(str(random.randint(0, 9999)) for _ in range(5000)).encode()
        cmds = ['cat', 'sort', 'uniq', 'nl', 'tee', 'cat', 'wc', 'cat']
        out_pipe, _, rc = self._run_pipe(*cmds, input_data=text)
        ref = self._run_shell("cat | sort | uniq | nl | tee /dev/null | cat | wc | cat", input_data=text)
        self.assertEqual(rc, 0)
        self.assertEqual(out_pipe, ref)

    def test_large_input(self):
        big = ("".join(random.choices(string.ascii_letters + string.digits, k=1_000_000)) + "\n").encode()
        out_pipe, _, rc = self._run_pipe('wc', input_data=big)
        ref = self._run_shell('wc', input_data=big)
        self.assertEqual(rc, 0)
        self.assertEqual(out_pipe, ref)

    # --------------- error propagation --------------- #
    def test_exit_status_first_error(self):
        """Ensure first failing command's status is propagated (false -> 1)."""
        _, _, rc = self._run_pipe('false', 'true', 'true')
        self.assertEqual(rc, 1)

    def test_invalid_command_middle(self):
        _, err, rc = self._run_pipe('echo', 'bogus_command_xyz', 'cat')
        self.assertNotEqual(rc, 0)
        self.assertTrue(err, 'stderr should contain exec failure message')

    # --------------- resource hygiene --------------- #
    def test_no_fd_leak(self):
        """Count descriptors before and after one run (delta ≤ 2)."""
        base = len(os.listdir('/proc/self/fd'))
        self._run_pipe('echo', 'wc')
        after = len(os.listdir('/proc/self/fd'))
        self.assertLessEqual(after - base, 2)

    def test_no_orphans_with_strace(self):
        trace = Path('adv_trace.log')
        subprocess.run(['strace', '-f', '-o', str(trace), './pipe', 'echo', 'wc', 'cat'], capture_output=True)
        fork = subprocess.run(['grep', '-c', 'clone(', str(trace)], capture_output=True, text=True)
        wait = subprocess.run(['grep', '-c', 'wait', str(trace)], capture_output=True, text=True)
        os.remove(trace)
        self.assertGreaterEqual(int(wait.stdout.strip()), int(fork.stdout.strip()))

    # --------------- robustness --------------- #
    def test_sigpipe_handling(self):
        """Consumer closes early; producer must not crash (yes | head -n1)."""
        res = subprocess.run(['./pipe', 'yes', 'head', '-n', '1'], capture_output=True, timeout=2)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout, b'y\n')

    def test_random_small_pipelines(self):
        pool = ['cat', 'tr a-z A-Z', 'wc', 'sort', 'uniq']
        for _ in range(20):
            length = random.randint(1, 5)
            combo = random.choices(pool, k=length)
            # split first token for pipe invocation (tr requires args handled by shell branch only)
            cmds = [c.split()[0] for c in combo]
            pipeline = ' | '.join(combo)
            data = ''.join(random.choices(string.ascii_letters + string.digits + '\n', k=4096)).encode()
            out_pipe, _, rc = self._run_pipe(*cmds, input_data=data)
            ref = self._run_shell(pipeline, input_data=data)
            self.assertEqual(rc, 0)
            self.assertEqual(out_pipe, ref)


if __name__ == '__main__':
    unittest.main()
