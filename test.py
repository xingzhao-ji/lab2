import os, random, string, subprocess, unittest, itertools, shlex
from pathlib import Path

class TestPipeFixed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = subprocess.run(['make'], capture_output=True, text=True)
        assert build.returncode == 0, f"make failed:\n{build.stdout}\n{build.stderr}"

    @classmethod
    def tearDownClass(cls):
        subprocess.run(['make', 'clean'], capture_output=True)

    # ---------- helpers ---------- #
    def _pipe(self, *argv, data=b""):
        res = subprocess.run(['./pipe', *argv], input=data, capture_output=True)
        return res.stdout, res.stderr, res.returncode

    def _shell(self, pipeline, data=b""):
        res = subprocess.run(pipeline, input=data, capture_output=True, shell=True)
        return res.stdout, res.stderr, res.returncode

    # ---------- positive paths ---------- #
    def test_single_cat(self):
        payload = b"hello\nworld\n"
        out, _, rc = self._pipe('cat', data=payload)
        self.assertEqual(rc, 0)
        self.assertEqual(out, payload)

    def test_eight_stage_pipeline(self):
        text = ("\n".join(str(random.randint(0, 9999)) for _ in range(5000)) + "\n").encode()
        stages = ['cat', 'sort', 'uniq', 'nl', 'tee', 'cat', 'wc', 'cat']
        out, _, rc = self._pipe(*stages, data=text)
        ref, _, _ = self._shell(' | '.join(stages), data=text)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    def test_big_input(self):
        blob = ("".join(random.choices(string.ascii_letters + string.digits, k=1_000_000)) + "\n").encode()
        out, _, rc = self._pipe('wc', data=blob)
        ref, _, _ = self._shell('wc', data=blob)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    # ---------- error propagation ---------- #
    def test_first_error_status(self):
        _, _, rc = self._pipe('false', 'true', 'true')
        self.assertEqual(rc, 1)

    def test_exec_failure_message(self):
        _, err, rc = self._pipe('echo', 'definitely_not_a_command_xyz', 'cat')
        self.assertNotEqual(rc, 0)
        self.assertTrue(err, 'stderr should contain exec failure message')

    # ---------- hygiene checks ---------- #
    def test_fd_leak(self):
        before = len(os.listdir('/proc/self/fd'))
        self._pipe('echo', 'wc')
        after = len(os.listdir('/proc/self/fd'))
        self.assertLessEqual(after - before, 2)

    def test_no_orphans_strace(self):
        trace = Path('trace.log')
        subprocess.run(['strace', '-f', '-o', str(trace), './pipe', 'echo', 'wc', 'cat'], capture_output=True)
        forks = int(subprocess.check_output(['grep', '-c', 'clone(', str(trace)]).strip())
        waits = int(subprocess.check_output(['grep', '-c', 'wait', str(trace)]).strip())
        trace.unlink()
        self.assertGreaterEqual(waits, forks)

    # ---------- SIGPIPE robustness ---------- #
    def test_sigpipe(self):
        res = subprocess.run(['./pipe', 'yes', 'head', '-n', '1'], capture_output=True, timeout=2)
        self.assertEqual(res.returncode, 0)
        self.assertEqual(res.stdout, b'y\n')

    # ---------- fixed random pipelines ---------- #
    def test_random_small_pipelines(self):
        pool = ['cat',
                'tr a-z A-Z',
                'tr A-Z a-z',
                'wc',
                'sort',
                'uniq']
        for _ in range(20):
            cmds = random.choices(pool, k=random.randint(1, 5))
            flat_argv = list(itertools.chain.from_iterable(shlex.split(c) for c in cmds))
            data = ''.join(random.choices(string.ascii_letters + string.digits + '\n', k=4096)).encode()

            out, _, rc = self._pipe(*flat_argv, data=data)
            ref, _, _ = self._shell(' | '.join(cmds), data=data)

            self.assertEqual(rc, 0, f"pipeline {' | '.join(cmds)} returned {rc}")
            self.assertEqual(out, ref)

    # ---------- extra corner cases ---------- #
    def test_single_letter_arg(self):
        out, _, rc = self._pipe('echo', 'a')
        self.assertEqual(rc, 0)
        self.assertEqual(out, b'a\n')

    def test_numeric_arg(self):
        out, _, rc = self._pipe('seq', '10')
        ref, _, _ = self._shell('seq 10')
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)

    def test_many_stages(self):
        stages = ['cat'] * 15 + ['wc']
        payload = b'foo\nbar\nbaz\n'
        out, _, rc = self._pipe(*stages, data=payload)
        ref, _, _ = self._shell(' | '.join(stages), data=payload)
        self.assertEqual(rc, 0)
        self.assertEqual(out, ref)


if __name__ == '__main__':
    unittest.main()
