# test_pipe.py
import os, shlex, signal, subprocess, tempfile, textwrap, unittest, uuid, pathlib

BIN = pathlib.Path('./pipe')          # produced by `make`
SHELL = ['/usr/bin/env', 'bash', '-c']

def _compile():
    """Invoke `make` once for the whole test‑run."""
    res = subprocess.run(['make'], capture_output=True, text=True)
    if res.returncode:
        raise RuntimeError(f"make failed:\n{res.stdout}\n{res.stderr}")

def _run_pipe(*argv, timeout=10):
    """Run ./pipe argv… and return (rc, out, err)."""
    r = subprocess.run([BIN, *argv], capture_output=True, text=True, timeout=timeout)
    return r.returncode, r.stdout, r.stderr

def _run_shell(pipeline, timeout=10):
    """Run a reference shell pipeline and return (rc, out, err)."""
    r = subprocess.run(SHELL + [pipeline], capture_output=True,
                       text=True, timeout=timeout)
    return r.returncode, r.stdout, r.stderr

class TestPipe(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        _compile()

    @classmethod
    def tearDownClass(cls):
        subprocess.run(['make', 'clean'], capture_output=True)

    # ---------- functional output -------------------------------------------------
    def _compare(self, cmds):
        """Compare ./pipe with an equivalent shell pipeline."""
        rc_p, out_p, err_p = _run_pipe(*sum(cmds, []))
        rc_s, out_s, err_s = _run_shell(' | '.join(map(shlex.join, cmds)))
        self.assertEqual(out_p, out_s, f"stdout mismatch for pipeline {cmds}")
        # stderr intentionally *not* compared: it is unspecified for success
        self.assertEqual(err_p, '',   f"./pipe wrote to stderr but pipeline succeeded")
        self.assertEqual(rc_p, rc_s if rc_s != 141 else 0,   # 141 == SIGPIPE
                         "exit status mismatch")

    def test_single_command(self):
        self._compare([['echo', 'Hello', 'world']])

    def test_two_commands_with_args(self):
        # echo 'Hello' | tr a-z A-Z
        self._compare([['echo', 'Hello'], ['tr', 'a-z', 'A-Z']])

    def test_three_commands(self):
        # seq 1 10 | grep 5 | wc -l   ⇒ “1\n”
        self._compare([['seq', '1', '10'],
                       ['grep', '5'],
                       ['wc', '-l']])

    def test_eight_stage_pipeline(self):
        cmds = [['printf', '%s\n' % i] for i in range(8)]
        # printf 0 … | cat | cat | … (8×) → exactly eight lines
        self._compare(cmds)

    # ---------- error handling ----------------------------------------------------
    def test_no_arguments(self):
        rc, _, _ = _run_pipe()
        self.assertEqual(rc, 22, "Expected errno EINVAL (22) when no argv given")

    def test_bogus_command(self):
        rc, _, err = _run_pipe('bogus‑cmd‑' + uuid.uuid4().hex)
        self.assertNotEqual(rc, 0, "bogus command should fail")
        self.assertTrue(err, "failure should appear on stderr")

    def test_first_command_fails(self):
        rc, _, _ = _run_pipe('false', 'true')
        self.assertEqual(rc, 1)

    def test_last_command_fails(self):
        rc, _, _ = _run_pipe('true', 'false')
        self.assertEqual(rc, 1)

    def test_sigpipe_is_zero(self):
        # yes | head -n 1   — yes gets SIGPIPE; pipeline should still succeed
        rc, out, _ = _run_pipe('yes', 'head', '-n', '1')
        self.assertEqual(out, 'y\n')
        self.assertEqual(rc, 0, "SIGPIPE should be mapped to exit‑status 0")

    # ---------- resource discipline ----------------------------------------------
    def test_no_orphans(self):
        """
        Use strace to count fork/clone vs wait; should be equal.
        This is cheap (tiny pipeline) yet catches fd‑leak/orphan mistakes.
        """
        with tempfile.TemporaryDirectory() as td:
            trace = pathlib.Path(td) / 'trace.log'
            subprocess.run(['strace', '-q', '-ff', '-o', trace, BIN, 'echo', 'hi', 'wc'],
                           check=True)
            clones = subprocess.check_output(['grep', '-o', 'clone(', *trace.glob('*')],
                                             text=True).count('clone(')
            waits  = subprocess.check_output(['grep', '-o', 'wait',   *trace.glob('*')],
                                             text=True).count('wait')
            self.assertGreaterEqual(waits, clones, "Every child should be waited for")

if __name__ == '__main__':           # pragma: no cover
    unittest.main()
