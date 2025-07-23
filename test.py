#!/usr/bin/env python3
import os, random, string, subprocess, unittest, itertools, shlex, shutil
from pathlib import Path
from typing import Tuple

BIN   = Path('./pipe')
MAKE  = ['make']
CLEAN = ['make', 'clean']

# ---------- helpers ---------- #
def _run_pipe(argv, data=b"") -> Tuple[bytes, bytes, int]:
    """Execute ./pipe <argv...> with optional stdin data."""
    res = subprocess.run([BIN, *argv], input=data, capture_output=True)
    return res.stdout, res.stderr, res.returncode

def _run_shell(pipeline: str, data=b"") -> Tuple[bytes, bytes, int]:
    """Execute the same pipeline via /bin/bash for ground‑truth comparison."""
    res = subprocess.run(pipeline, input=data, capture_output=True,
                         shell=True, executable='/bin/bash')
    return res.stdout, res.stderr, res.returncode

def _rand_text(n: int) -> bytes:
    return ''.join(random.choices(string.ascii_letters + string.digits + '\n', k=n)).encode()

# ---------- test‑suite ---------- #
class TestPipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = subprocess.run(MAKE, capture_output=True, text=True)
        if build.returncode != 0:
            raise RuntimeError(f"`make` failed:\n{build.stdout}\n{build.stderr}")
        if not BIN.exists():
            raise RuntimeError("build succeeded but ./pipe missing")

    @classmethod
    def tearDownClass(cls):
        subprocess.run(CLEAN, capture_output=True)

    # ===== functional happy paths ===== #
    def test_single_stage_cat(self):
        payload = b"hello\nworld\n"
        out, _, rc = _run_pipe(['cat'], payload)
        self.assertEqual(rc, 0)
        self.assertEqual(out, payload)

    def test_two_stage_grep(self):
        text = b"a\nb\nc\n"
        out, _, rc = _run_pipe(['cat', 'grep', 'b'], text)
        self.assertEqual(rc, 0)
        self.assertEqual(out, b"b\n")

    def test_eight_stage_mixed(self):
        data = _rand_text(32_000)
        stages = ['cat', 'tr a-z A-Z', 'tr A-Z a-z', 'sort', 'uniq', 'nl', 'tee', 'wc']
        flat = list(itertools.chain.from_iterable(shlex.split(s) for s in stages))
        out, _, rc = _run_pipe(flat, data)
        ref, _, _  = _run_shell(' | '.join(stages), data)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    def test_twenty_stage_chain(self):
        chain = ['cat']*19 + ['wc']
        data  = b'foo\nbar\nbaz\n'
        flat  = list(itertools.chain.from_iterable(shlex.split(c) for c in chain))
        out, _, rc = _run_pipe(flat, data)
        ref, _, _  = _run_shell(' | '.join(chain), data)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    def test_big_binary_input(self):
        blob = os.urandom(1_048_576)  # 1 MiB of random bytes
        out, _, rc = _run_pipe(['wc', '-c'], blob)
        ref, _, _  = _run_shell('wc -c', blob)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    # ===== argument / error handling ===== #
    def test_no_arguments_errno_22(self):
        _, _, rc = _run_pipe([])
        self.assertEqual(rc, 22)

    def test_first_error_code_propagates(self):
        _, _, rc = _run_pipe(['false', 'true', 'true'])
        self.assertEqual(rc, 1)

    def test_exec_failure_stderr(self):
        _, err, rc = _run_pipe(['definitely_not_cmd_xyz'])
        self.assertNotEqual(rc, 0)
        self.assertIn(b'not_cmd', err.lower())

    # ===== SIGPIPE robustness ===== #
    def test_sigpipe_ignored(self):
        res = subprocess.run([BIN, 'yes', 'head', '-n', '1'],
                             capture_output=True, timeout=2)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout, b'y\n')

    # ===== resource‑hygiene tests ===== #
    @unittest.skipUnless((Path('/proc')/ 'self' / 'fd').exists(),
                         "/proc not present to count FDs")
    def test_fd_leak(self):
        before = len(os.listdir('/proc/self/fd'))
        _run_pipe(['echo', 'wc'])
        after  = len(os.listdir('/proc/self/fd'))
        self.assertLessEqual(after - before, 2,
            "child FDs not closed (expect ≤2 extra)")

    @unittest.skipUnless(shutil.which('strace'),  'strace not installed')
    def test_parent_waits_for_children(self):
        trace = Path('trace.log')
        subprocess.run(['strace', '-f', '-qq', '-o', str(trace),
                        BIN, 'echo', 'wc'], capture_output=True)
        forks = int(subprocess.check_output(['grep', '-c', 'clone(', str(trace)]).strip() or b'0')
        waits = int(subprocess.check_output(['grep', '-c', 'wait',   str(trace)]).strip() or b'0')
        trace.unlink(missing_ok=True)
        self.assertGreaterEqual(waits, forks,
            "parent didn't wait‑pid for every forked child")

    # ===== randomised equivalence checks ===== #
    def test_50_random_pipelines(self):
        pool = ['cat',
                'tr a-z A-Z',
                'tr A-Z a-z',
                'wc',
                'sort',
                'uniq',
                'grep -v ^$']
        for _ in range(50):
            cmds  = random.choices(pool, k=random.randint(1, 6))
            argv  = list(itertools.chain.from_iterable(shlex.split(c) for c in cmds))
            data  = _rand_text(4096)
            out, _, rc = _run_pipe(argv, data)
            ref, _, _  = _run_shell(' | '.join(cmds), data)
            self.assertEqual(rc, 0,
                             f"pipeline {' | '.join(cmds)} exited {rc}")
            self.assertEqual(out, ref,
                             f"mismatch on pipeline {' | '.join(cmds)}")

    # ===== optional tooling ===== #
    @unittest.skipUnless(shutil.which('valgrind'), 'valgrind not installed')
    def test_valgrind_no_leaks(self):
        vg = subprocess.run(['valgrind', '--quiet',
                             '--error-exitcode=99',
                             '--leak-check=full',
                             BIN, 'true'],
                            capture_output=True)
        self.assertEqual(vg.returncode, 0,
            f"valgrind detected leaks or errors:\n{vg.stderr.decode()}")

if __name__ == '__main__':
    random.seed(0xC0FFEE)
    unittest.main(verbosity=2)
