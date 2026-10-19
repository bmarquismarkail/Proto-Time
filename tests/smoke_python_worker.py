"""Resource setup must fail visibly before any automation code is compiled."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import resource
import subprocess
import sys
import unittest
from unittest.mock import patch

worker_path = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location('python_worker', worker_path)
worker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(worker)
request = {'heapBytes': 8 * 1024 * 1024, 'timeoutMs': 500,
           'source': 'raise RuntimeError("user code ran")'}


class ResourceSetup(unittest.TestCase):
    def check_failure(self, target, error, limit=None):
        output = io.StringIO()

        def set_limit(kind, values):
            if target == 'setrlimit' and kind == limit:
                raise error

        with contextlib.ExitStack() as stack:
            stack.enter_context(patch.object(sys, 'stdin', io.StringIO(json.dumps(request))))
            stack.enter_context(contextlib.redirect_stdout(output))
            stack.enter_context(patch.object(resource, 'setrlimit', side_effect=set_limit))
            stack.enter_context(patch.object(worker.tracemalloc, 'start'))
            compile_script = stack.enter_context(patch.object(worker, 'compile', create=True))
            if target != 'setrlimit':
                module, attribute = {
                    'open': (worker, 'open'),
                    'sysconf': (worker.os, 'sysconf'),
                    'tracemalloc': (worker.tracemalloc, 'start'),
                }[target]
                stack.enter_context(patch.object(module, attribute, side_effect=error, create=True))
            worker.main()
            compile_script.assert_not_called()
        result = json.loads(output.getvalue())
        self.assertFalse(result['success'])
        self.assertIn('Python resource setup failed:', result['error'])
        self.assertIn(str(error), result['error'])
        self.assertNotIn('registers', result)
        self.assertNotIn('writes', result)

    def test_system_call_failures(self):
        for limit in (resource.RLIMIT_AS, resource.RLIMIT_CORE,
                      resource.RLIMIT_FSIZE, resource.RLIMIT_CPU):
            for error in (OSError('limit syscall failed'), ValueError('inherited hard limit')):
                with self.subTest(limit=limit, error=type(error).__name__):
                    self.check_failure('setrlimit', error, limit)
        for target, error in (('open', OSError('statm unavailable')),
                              ('sysconf', ValueError('page size unavailable')),
                              ('tracemalloc', MemoryError('tracing allocation failed'))):
            with self.subTest(target=target):
                self.check_failure(target, error)

    def test_inherited_hard_limit(self):
        # An actual kernel-enforced restriction: the worker cannot raise this
        # hard limit to its requested two-second CPU budget.
        def restrict_cpu():
            resource.setrlimit(resource.RLIMIT_CPU, (1, 1))

        result = subprocess.run([sys.executable, '-I', '-S', str(worker_path)],
                                input=json.dumps(request), capture_output=True,
                                text=True, timeout=10, preexec_fn=restrict_cpu)
        self.assertEqual(result.returncode, 0, result.stderr)
        reply = json.loads(result.stdout)
        self.assertFalse(reply['success'])
        self.assertIn('Python resource setup failed:', reply['error'])
        self.assertNotIn('user code ran', reply['error'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
