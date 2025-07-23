# test_pipe_final.py
import os, pathlib, shlex, shutil, signal, subprocess, tempfile, unittest

BIN   = './pipe'                          # <<< fixed
SHELL = ['/usr/bin/env', 'bash', '-c']

def _compile():
    res = subprocess.run(['make'], capture_output=True, text=True)
    if res.returncode or not pathlib.Path(BIN).exists():
        raise RuntimeError(f"`make` did not create {BIN}:\n{res.stdout}\n{res.stderr}")

def _run_pipe(*argv, timeout=10):
    r = subprocess.run([BIN, *argv], capture_output=True,
                       text=True, timeout=timeout)
    return r.returncode, r.stdout, r.stderr

def _run_shell(cmds, timeout=10):
    r = subprocess.run(SHELL + [' | '.join(cmds)], capture_output=True,
                       text=True, timeout=timeout)
    return r.returncode, r.stdout, r.stderr

class TestPipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _compile()

    @classmethod
    def tearDownClass(cls):
        subprocess.run(['make', 'clean'], capture_output=True)

    # ---------- helpers ----------------------------------------------------------
    def _compare(self, cmds):
        rc_p, out_p, err_p = _run_pipe(*cmds)
        rc_s, out_s, err_s = _run_shell(cmds)
        self.assertEqual(out_p, out_s, f"stdout mismatch for {cmds}")
        self.assertEqual(err_p,     '', "stderr should be empty on success")
        self.assertEqual(rc_p,  rc_s,  "exit‑status mismatch")

    # ---------- functional output ------------------------------------------------
    def test_single_command(self):
        self._compare(['ls'])

    def test_two_stage(self):
        self._compare(['echo', 'wc'])           # echo | wc

    def test_three_stage(self):
        self._compare(['ls', 'cat', 'wc'])      # ls | cat | wc

    def test_eight_stage(self):
        cmds = ['echo'] + ['cat']*6 + ['wc']    # total 8 programs
        self._compare(cmds)

    # ---------- error paths ------------------------------------------------------
    def test_no_arguments(self):
        rc, _, _ = _run_pipe()
        self.assertEqual(rc, 22)                # EINVAL

    def test_bogus_command(self):
        rc, _, err = _run_pipe('definitely_not_real')
        self.assertNotEqual(rc, 0)
        self.assertTrue(err)

    def test_first_command_fails(self):
        rc, _, _ = _run_pipe('false', 'true')   # first stage fails
        self.assertEqual(rc, 1)

    def test_last_command_fails(self):
        rc, _, _ = _run_pipe('true', 'false')   # last stage fails
        self.assertEqual(rc, 1)

    def test_sigpipe_maps_to_zero(self):
        rc, out, _ = _run_pipe('yes', 'head')   # yes gets SIGPIPE; pipeline succeeds
        self.assertEqual(out.count('\n'), 10)   # head default = 10 lines
        self.assertEqual(rc, 0)

    # ---------- resource discipline ---------------------------------------------
    def test_no_orphans(self):
        if shutil.which('strace') is None:
            self.skipTest("strace unavailable")
        with tempfile.TemporaryDirectory() as td:
            log = pathlib.Path(td) / 'trace'
            subprocess.run(['strace', '-q', '-ff', '-o', str(log),
                            BIN, 'echo', 'cat', 'wc'], check=True)
            clones = subprocess.check_output(
                ['grep', '-o', 'clone(', *log.parent.glob('trace*')], text=True
            ).count('clone(')
            waits = subprocess.check_output(
                ['grep', '-o', 'wait', *log.parent.glob('trace*')], text=True
            ).count('wait')
            self.assertGreaterEqual(waits, clones,
                                    "each child must be waited for")

if __name__ == '__main__':           # pragma: no cover
    unittest.main()
