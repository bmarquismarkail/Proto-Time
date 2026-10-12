"""Isolated automation worker; guest mutations are returned, never committed here.

This is a resource-limited host-code worker, not a security sandbox. Runtime
imports and interpreter setup happen before the invocation budget begins.
"""
import contextlib
import io
import json
import math
import os
import resource
import sys
import time as clock
import tracemalloc


def main():
    request = json.load(sys.stdin)
    maximum = request['heapBytes']
    # Preserve interpreter startup mappings, then bound additional address space.
    with open('/proc/self/statm', encoding='ascii') as stream:
        current = int(stream.read().split()[0]) * os.sysconf('SC_PAGE_SIZE')
    resource.setrlimit(resource.RLIMIT_AS, (current + maximum, current + maximum))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (1048576, 1048576))
    resource.setrlimit(resource.RLIMIT_CPU, (math.ceil(request['timeoutMs'] / 1000) + 1,) * 2)
    tracemalloc.start()
    deadline = clock.monotonic() + request['timeoutMs'] / 1000
    remaining = request['instructions']
    failed = False
    permission_denied = False
    registers = dict(request['registers'])
    writes = []
    report = []
    report_bytes = 0
    edited = False

    def reject(message):
        nonlocal failed, permission_denied
        failed = True
        permission_denied |= "denied" in message
        raise ValueError(message)

    def integer(value, maximum):
        if type(value) is not int or not 0 <= value <= maximum:
            reject('binding integer rejected')
        return value

    class Bindings:
        def reg(self, name):
            if type(name) is not str or name not in registers:
                reject('unknown register')
            return registers[name]

        def setreg(self, name, value):
            nonlocal edited
            value = integer(value, 65535)
            if not request['permissions']['registers'] or name not in registers:
                reject('register mutation denied or invalid')
            if value > request['maximums'][name] or (request['core'] == 'gameboy' and name == 'AF' and value & 15):
                reject('register value rejected')
            registers[name] = value
            edited = True

        def read8(self, address):
            address = integer(address, 65535)
            for edit in reversed(writes):
                if edit['address'] == address:
                    return edit['value']
            offset = address - request['memoryBase']
            if not 0 <= offset < len(request['memory']):
                reject('address outside owned snapshot')
            return request['memory'][offset]

        def write8(self, address, value):
            address, value = integer(address, 65535), integer(value, 255)
            if not request['permissions']['ram'] or len(writes) >= 64:
                reject('RAM mutation permission/budget rejected')
            writes.append({'address': address, 'value': value})

        def report(self, text):
            nonlocal report_bytes
            if type(text) is not str or not request['permissions']['reports']:
                reject('report denied or invalid')
            size = len(text.encode('utf-8'))
            if size > 65536 - report_bytes:
                reject('report budget rejected')
            report_bytes += size
            report.append(text)

    def trace(frame, event, arg):
        nonlocal remaining
        frame.f_trace_opcodes = True
        if event == 'opcode':
            remaining -= 1
        if remaining < 0 or clock.monotonic() > deadline or tracemalloc.get_traced_memory()[1] > maximum:
            reject('Python instruction/time/heap budget exhausted')
        return trace

    # Script stdout is deliberately not the mutation protocol. Unbounded printed
    # output uses the same heap limit and fails the invocation when exhausted.
    output = io.StringIO()
    try:
        code = compile(request['source'], 'automation.py', 'exec')
        sys.settrace(trace)
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            exec(code, {'time': Bindings(), '__name__': '__time_script__'})
        sys.settrace(None)
        if failed or remaining < 0 or clock.monotonic() > deadline or tracemalloc.get_traced_memory()[1] > maximum:
            raise ValueError('Python invocation rejected')
        result = {'success': True, 'registers': registers if edited else {}, 'writes': writes, 'report': ''.join(report)}
    except BaseException as error:
        sys.settrace(None)
        result = {'success': False, 'permissionDenied': permission_denied, 'error': str(error)[:4096]}
    sys.stdout.write(json.dumps(result, ensure_ascii=True))
    sys.stdout.flush()


if __name__ == '__main__':
    try:
        main()
    except BaseException:
        # The parent treats missing/truncated protocol output as failure.
        sys.exit(1)
