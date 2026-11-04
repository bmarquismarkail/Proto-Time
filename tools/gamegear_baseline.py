#!/usr/bin/env python3
"""Collect serial, live SDL Game Gear compatibility evidence without changing guest code.

X11 events are synthetic and window-targeted; they never establish physical input
acceptance. Pulse monitor capture includes other applications using the same sink.
"""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
CLOCK_HZ = 3579545


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def command(args, cwd=ROOT):
    try:
        p = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=20)
        return {'command': list(map(str, args)), 'exit_code': p.returncode,
                'stdout': p.stdout, 'stderr': p.stderr}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {'command': list(map(str, args)), 'error': str(e)}


def rom_identity(path):
    data = Path(path).read_bytes()
    headers = []
    for offset in (0x1ff0, 0x3ff0, 0x7ff0):
        if len(data) >= offset + 16 and data[offset:offset + 8] == b'TMR SEGA':
            headers.append({'offset': offset, 'bytes_hex': data[offset:offset + 16].hex(),
                            'region_code': data[offset + 15] >> 4,
                            'size_code': data[offset + 15] & 15})
    return {'path': str(Path(path).resolve()), 'sha256': sha(path),
            'size_bytes': len(data), 'sega_headers': headers}


class KeyEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int),
                ('display', C.c_void_p), ('window', C.c_ulong), ('root', C.c_ulong),
                ('subwindow', C.c_ulong), ('time', C.c_ulong), ('x', C.c_int),
                ('y', C.c_int), ('x_root', C.c_int), ('y_root', C.c_int),
                ('state', C.c_uint), ('keycode', C.c_uint), ('same_screen', C.c_int)]


class Event(C.Union):
    _fields_ = [('key', KeyEvent), ('pad', C.c_long * 24)]


class XWindow:
    """Only sends events to the window belonging to the child PID; never grabs focus."""
    def __init__(self):
        self.x = C.CDLL('libX11.so.6')
        specs = {
            'XOpenDisplay': ([C.c_char_p], C.c_void_p),
            'XDefaultRootWindow': ([C.c_void_p], C.c_ulong),
            'XQueryTree': ([C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_ulong),
                            C.POINTER(C.POINTER(C.c_ulong)), C.POINTER(C.c_uint)], C.c_int),
            'XInternAtom': ([C.c_void_p, C.c_char_p, C.c_int], C.c_ulong),
            'XGetWindowProperty': ([C.c_void_p, C.c_ulong, C.c_ulong, C.c_long, C.c_long,
                                   C.c_int, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_int),
                                   C.POINTER(C.c_ulong), C.POINTER(C.c_ulong),
                                   C.POINTER(C.POINTER(C.c_ubyte))], C.c_int),
            'XStringToKeysym': ([C.c_char_p], C.c_ulong),
            'XKeysymToKeycode': ([C.c_void_p, C.c_ulong], C.c_uint),
            'XSendEvent': ([C.c_void_p, C.c_ulong, C.c_int, C.c_long, C.POINTER(Event)], C.c_int),
            'XFlush': ([C.c_void_p], C.c_int),
            'XSync': ([C.c_void_p, C.c_int], C.c_int),
            'XFetchName': ([C.c_void_p, C.c_ulong, C.POINTER(C.c_char_p)], C.c_int), 'XFree': ([C.c_void_p], C.c_int),
            'XCloseDisplay': ([C.c_void_p], C.c_int),
        }
        for name, (args, result) in specs.items():
            fn = getattr(self.x, name); fn.argtypes = args; fn.restype = result
        self.display = self.x.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('Live X11 display unavailable; refusing dummy/headless substitution')
        # Desktop windows can disappear during PID discovery. X errors must not
        # terminate the collector or discard already written evidence.
        self.error_count = 0
        def note_error(display, error):
            self.error_count += 1
            return 0
        self.error_callback = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)(note_error)
        self.x.XSetErrorHandler.argtypes = [type(self.error_callback)]
        self.x.XSetErrorHandler(self.error_callback)
        self.root = self.x.XDefaultRootWindow(self.display)
        self.atom = self.x.XInternAtom(self.display, b'_NET_WM_PID', 0)

    def close(self):
        if self.display:
            self.x.XCloseDisplay(self.display); self.display = None

    def find(self, pid, window=None):
        window = self.root if window is None else window
        actual = C.c_ulong(); fmt = C.c_int(); n = C.c_ulong(); remain = C.c_ulong()
        data = C.POINTER(C.c_ubyte)()
        result = self.x.XGetWindowProperty(self.display, window, self.atom, 0, 1, 0, 0,
                                          C.byref(actual), C.byref(fmt), C.byref(n),
                                          C.byref(remain), C.byref(data))
        match = False
        if result == 0 and data:
            match = fmt.value == 32 and n.value == 1 and C.cast(data, C.POINTER(C.c_ulong))[0] == pid
            self.x.XFree(data)
        if match:
            name = C.c_char_p()
            if self.x.XFetchName(self.display, window, C.byref(name)) and name:
                title = name.value.decode(errors='replace')
                self.x.XFree(name)
                if title.startswith('Proto-Time - '): return window
        root = C.c_ulong(); parent = C.c_ulong(); count = C.c_uint()
        children = C.POINTER(C.c_ulong)()
        if self.x.XQueryTree(self.display, window, C.byref(root), C.byref(parent),
                             C.byref(children), C.byref(count)):
            values = list(children[:count.value]) if children else []
            if children: self.x.XFree(children)
            for child in values:
                found = self.find(pid, child)
                if found: return found
        return None

    def key(self, window, name, down):
        code = self.x.XKeysymToKeycode(self.display, self.x.XStringToKeysym(name.encode()))
        if not code: raise ValueError('Unknown X11 key: ' + name)
        event = Event()
        event.key = KeyEvent(type=2 if down else 3, display=self.display, window=window,
                             root=self.root, same_screen=1, keycode=code)
        before_errors = self.error_count
        sent = self.x.XSendEvent(self.display, window, 0, 1 if down else 2, C.byref(event))
        self.x.XSync(self.display, 0)
        return bool(sent and self.error_count == before_errors)


def read_samples(path):
    samples, errors = [], []
    if path.exists():
        for i, line in enumerate(path.read_text().splitlines(), 1):
            try:
                value = json.loads(line)
                if not isinstance(value, dict): raise ValueError('sample is not an object')
                samples.append(value)
            except (ValueError, TypeError) as e: errors.append(f'line {i}: {e}')
    return samples, errors


def get(value, path):
    for key in path.split('.'):
        if not isinstance(value, dict): return None
        value = value.get(key)
    return value


def summarize(samples, warmup, complete):
    intervals = []
    for a, b in zip(samples, samples[1:]):
        if not all(isinstance(row.get(key), int) and row[key] >= 0
                   for row in (a, b) for key in ('host_elapsed_ns', 'emulated_cycles')):
            continue
        start = a.get('host_elapsed_ns', 0) / 1e9
        dt = (b.get('host_elapsed_ns', 0) - a.get('host_elapsed_ns', 0)) / 1e9
        if start < warmup or dt <= 0: continue
        dc = b.get('emulated_cycles', 0) - a.get('emulated_cycles', 0)
        if dc < 0: continue
        intervals.append({'start_seconds': start, 'seconds': dt, 'speed': dc / dt / CLOCK_HZ})
    bad = [x for x in intervals if not .95 <= x['speed'] <= 1.05]
    sustained = any(not .95 <= a['speed'] <= 1.05 and not .95 <= b['speed'] <= 1.05
                    for a, b in zip(intervals, intervals[1:]))
    measured = [s for s in samples if s.get('host_elapsed_ns', 0) / 1e9 >= warmup]
    paths = ['audio.underruns_after_priming', 'audio.silence_samples_after_priming',
             'audio.ready_queue.drop_count', 'audio.ready_queue.depth_last',
             'audio.ready_queue.depth_high_water', 'audio.ready_queue.depth_low_water',
             'audio.drain.duration.p50_ns', 'audio.drain.duration.p95_ns',
             'audio.drain.duration.p99_ns', 'audio.drain.duration.p999_ns',
             'timing.wake_jitter_over_2ms', 'video.frames_presented',
             'video.presenter_duration', 'video.publish_to_present_age', 'timing']
    last = samples[-1] if samples else {}
    metrics = {p: get(last, p) for p in paths}
    loss = {}
    loss_by_phase = {}
    for p in paths[:3]:
        values = [get(s, p) for s in measured]
        values = [v for v in values if isinstance(v, (int, float))]
        # These counters already exclude priming; include all recorded post-prime losses.
        loss[p] = max(values, default=None)
        monotonic = all(a <= b for a, b in zip(values, values[1:]))
        loss_by_phase[p] = {'startup_end_total': values[0] if values else None,
                            'after_startup_increment': values[-1] - values[0] if values and monotonic else None}
    audio_fail = any(v is not None and v > 0 for v in loss.values())
    return {'statuses': {'rendering': 'unverified', 'audio': 'fail' if audio_fail else 'unverified',
                         'input': 'unverified',
                         'timing': ('fail' if sustained else 'pass') if complete and intervals else 'unverified'},
            'human_review': {'rendering': 'pending', 'audible_output': 'pending',
                             'physical_keyboard': 'pending', 'controller': 'unavailable'},
            'intervals': intervals, 'outside_speed_band_count': len(bad),
            'sustained_outside_speed_band': sustained,
            'median_speed': statistics.median(x['speed'] for x in intervals) if intervals else None,
            'final_metrics': metrics, 'post_prime_losses': loss, 'loss_by_phase': loss_by_phase,
            'metric_note': 'Final percentiles/high-water marks are lifetime aggregates, not warmup-subtracted.'}


def check_button_images(meta):
    """Read the ROM's checkbox indicators; this is synthetic-input evidence only."""
    if meta['case']['id'] != 'buttons': return {'status': 'unverified', 'reason': 'No ROM-specific indicator contract'}
    try:
        from PIL import Image
    except ImportError:
        return {'status': 'unverified', 'reason': 'Pillow unavailable'}
    points = {'up': (15, 99), 'down': (15, 115), 'left': (71, 99),
              'right': (71, 115), 'button1': (135, 99), 'button2': (135, 115), 'start': (71, 131)}
    captures = {c['name']: c for c in meta.get('checkpoints', [])}
    observations = []
    for name in points:
        for phase in ('down', 'released'):
            capture = captures.get(name + '-' + phase)
            if not capture or not Path(capture['path']).is_file():
                return {'status': 'unverified', 'reason': 'Missing paired press/release image'}
            with Image.open(capture['path']) as image:
                image = image.convert('RGB')
                if abs(image.width / image.height - 160 / 144) > .01:
                    return {'status': 'unverified', 'reason': 'Unexpected capture aspect; checkbox alignment unavailable'}
                observed = {}
                for control, (x, y) in points.items():
                    pixel = image.getpixel((int((x + .5) * image.width / 160), int((y + .5) * image.height / 144)))
                    observed[control] = 'down' if max(pixel) < 32 else ('released' if min(pixel) > 223 else 'unknown')
                expected = {control: 'down' if control == name and phase == 'down' else 'released' for control in points}
                observations.append({'checkpoint': name + '-' + phase, 'observed': observed,
                                     'expected': expected, 'matches': observed == expected})
    return {'status': 'pass' if all(o['matches'] for o in observations) else 'fail',
            'route': 'Window-targeted synthetic events -> guest ROM checkbox indicators',
            'physical_keyboard': 'unverified', 'observations': observations}


def assess_run(meta, samples, errors, log, duration):
    last = samples[-1] if samples else {}
    live = bool(meta.get('window_id') and meta.get('device_stream')
                and 'Audio output: SDL2 audio output module' in log
                and get(last, 'audio.drain.callback_count')
                and get(last, 'video.frames_presented'))
    captures = meta.get('checkpoints', [])
    expected = {c['name'] for c in meta['case']['checkpoints'] if c['at'] < duration}
    captures_ok = bool(expected) and expected == {c['name'] for c in captures}
    captures_ok = captures_ok and all(c['capture'].get('exit_code') == 0 and Path(c['path']).is_file() for c in captures)
    complete = bool(captures_ok and meta.get('reached_duration') and meta.get('exit_code') == 0
                    and live and not errors and last.get('host_elapsed_ns', 0) / 1e9 >= duration - 2)
    return live, complete


def stop(process):
    if process.poll() is None:
        process.send_signal(signal.SIGINT)
        try: process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait()


def prepare_case(case, duration, index):
    case = json.loads(json.dumps(case))
    if index == 'soak':
        # Exercise live gameplay without blindly walking into pits/game-over.
        # Scrolling and directional movement remain in the ordinary scenarios.
        case['inputs'] = [e for e in case['inputs'] if e['key'] not in ('Left', 'Right', 'Up', 'Down')]
        for at in range(120, int(duration) - 10, 20):
            case['inputs'].extend([{'at': at + 2, 'key': 'z', 'hold': .3},
                                   {'at': at + 5, 'key': 'x', 'hold': .3}])
        case['checkpoints'].extend({'at': at, 'name': f'soak-{at}s'}
                                   for at in range(120, int(duration), 120))
        case['checkpoints'].append({'at': duration - 5, 'name': 'soak-final'})
        case['scenario_note'] = 'Gameplay soak near the starting area: jump/fire; no blind horizontal movement. Confirm gameplay in periodic captures.'
        case['expected_checkpoints'] = ['Remain in the admitted stage throughout the periodic captures',
                                        'Guest timer/animations and device audio continue',
                                        'Jump/fire inputs respond; scrolling is covered by ordinary runs']
    return case


def run_case(case, duration, index, directory, emulator, x, sink):
    case = prepare_case(case, duration, index)
    out = directory / f'{case["id"]}-{index}'
    out.mkdir()  # Never overwrite an earlier attempt.
    diag = out / 'diagnostics.jsonl'
    args = [str(emulator), '--core', 'gamegear', '--rom', case['identity']['path'],
            '--frontend', 'sdl', '--cpu-mode', 'baseline', '--timing-profile', 'balanced',
            '--speed', '1', '--audio-backend', 'sdl', '--scale', '3',
            '--diagnostics-report', str(diag), '--diagnostics-interval-ms', '1000']
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='pulseaudio')
    if sink: env['PULSE_SINK'] = sink
    meta = {'case': case, 'command': args, 'duration_seconds': duration,
            'warmup_seconds': 10, 'initial_load_average': os.getloadavg(), 'environment': {k: env.get(k) for k in
            ('DISPLAY', 'WAYLAND_DISPLAY', 'SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'PULSE_SINK')},
            'input_route': 'XSendEvent to emulator PID window; synthetic, not physical',
            'audio_capture_route': f'PulseAudio/PipeWire sink monitor {sink}.monitor; shared sink, may include other apps',
            'events': [], 'checkpoints': [], 'started_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}
    write_json(out / 'metadata.json', meta)
    started = time.monotonic(); process = None; recorder = None; window = None; held = set()
    actions = []
    for item in case['inputs']:
        actions.extend([(item['at'], 'key', item['key'], True),
                        (item['at'] + item['hold'], 'key', item['key'], False)])
    for item in case['checkpoints']:
        actions.append((item['at'], 'capture', item['name'], False))
    actions.sort(key=lambda a: a[0])
    try:
        with (out / 'process.log').open('w') as log, (out / 'audio-capture.log').open('w') as audio_log:
            process = subprocess.Popen(args, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            if sink:
                recorder = subprocess.Popen(['ffmpeg', '-nostdin', '-v', 'warning', '-f', 'pulse',
                    '-i', sink + '.monitor', '-t', str(duration), '-ac', '2', '-ar', '48000',
                    str(out / 'device-monitor.wav')], stdout=audio_log, stderr=subprocess.STDOUT, start_new_session=True)
            while process.poll() is None and time.monotonic() - started < duration:
                elapsed = time.monotonic() - started
                if elapsed >= 5 and 'device_stream' not in meta:
                    streams = command(['pactl', '-f', 'json', 'list', 'sink-inputs'])
                    try:
                        meta['device_stream'] = [entry for entry in json.loads(streams.get('stdout', '[]'))
                            if str(entry.get('properties', {}).get('application.process.id')) == str(process.pid)]
                    except ValueError:
                        meta['device_stream'] = []
                    write_json(out / 'metadata.json', meta)
                # SDL can replace its startup window when the renderer initializes.
                # Refresh the PID/title match instead of retaining that stale XID.
                window = x.find(process.pid)
                if window:
                    while actions and actions[0][0] <= elapsed:
                        at, kind, value, down = actions.pop(0)
                        if kind == 'key':
                            sent = x.key(window, value, down)
                            if down: held.add(value)
                            else: held.discard(value)
                            meta['events'].append({'planned_seconds': at, 'actual_seconds': elapsed,
                                                   'key': value, 'down': down, 'sent': sent, 'window_id': window})
                        else:
                            target = out / (value + '.png')
                            result = command(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'x11grab',
                                '-window_id', str(window), '-draw_mouse', '0', '-i', env['DISPLAY'], '-frames:v', '1',
                                '-threads', '1', str(target)])
                            meta['checkpoints'].append({'name': value, 'actual_seconds': elapsed,
                                                       'capture': result, 'path': str(target)})
                        write_json(out / 'metadata.json', meta)
                time.sleep(.05)
            meta['reached_duration'] = time.monotonic() - started >= duration
    finally:
        if process:
            if process.poll() is None and window:
                for key in held: x.key(window, key, False)
            stop(process)
        if recorder: stop(recorder)
        meta.update(elapsed_seconds=time.monotonic() - started,
                    exit_code=process.returncode if process else None, window_id=window,
                    recorder_exit_code=recorder.returncode if recorder else None)
        write_json(out / 'metadata.json', meta)
    samples, errors = read_samples(diag)
    log = (out / 'process.log').read_text()
    live, complete = assess_run(meta, samples, errors, log, duration)
    report = summarize(samples, 10, complete)
    report['automated_input_check'] = check_button_images(meta)
    report.update(case_id=case['id'], run=index, artifact_dir=str(out), complete=complete,
                  live_device_evidence=live, diagnostics_errors=errors,
                  remaining_actions=actions, sample_count=len(samples))
    write_json(out / 'result.json', report)
    return report


def write_matrix(output, session):
    lines = ['# Game Gear device evidence', '',
             f"Source: `{session['commit']}`; kind: {session['kind']}; protocol: {session['protocol']}; session: {session['status']}.", '',
             '| ROM | Runs collected | Complete | Rendering | Audio | Input | Timing |',
             '|---|---:|---:|---|---|---|---|']
    for case in session['cases']:
        runs = [r for r in session['results'] if r['case_id'] == case['id']]
        expected = (int(case.get('soak', False)) if session['parameters'].get('soaks_only') else
                    session['parameters']['repetitions'] + int(case.get('soak', False) and not session['parameters']['skip_soaks']))
        states = {}
        for field in ('rendering', 'audio', 'input', 'timing'):
            vals = [r['statuses'][field] for r in runs]
            states[field] = 'fail' if 'fail' in vals else ('pass' if len(runs) == expected and all(r['complete'] for r in runs) and all(v == 'pass' for v in vals) else 'unverified')
        lines.append(f"| {case['id']} | {len(runs)}/{expected} | {sum(r['complete'] for r in runs)} | " + ' | '.join(states.values()) + ' |')
    lines += ['', 'Human rendering, audible-output, and physical keyboard review remain pending.',
              'Synthetic event delivery does not establish physical input acceptance.',
              'See session.json and per-run result.json for intervals and raw artifact references.', '']
    (output / 'matrix.md').write_text('\n'.join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=ROOT / '.internal/docs/gamegear-baseline/corpus.json')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--duration', type=float, default=120, help='Ordinary run duration; soaks are always 600 seconds')
    parser.add_argument('--skip-soaks', action='store_true')
    parser.add_argument('--soaks-only', action='store_true')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--kind', choices=['baseline', 'candidate'], default='baseline')
    parser.add_argument('--baseline-session', type=Path)
    parser.add_argument('--master-verification', type=Path, help='Earlier successful session.json remote-master verification for this campaign')
    args = parser.parse_args()
    if args.soaks_only and args.skip_soaks: parser.error('--soaks-only and --skip-soaks are mutually exclusive')
    if args.duration <= 10 or args.repetitions < 1: parser.error('duration must exceed 10s and repetitions must be positive')
    baseline = None
    if args.kind == 'candidate':
        if not args.baseline_session:
            parser.error('candidate requires --baseline-session pointing to preserved baseline evidence')
        baseline = json.loads((args.baseline_session / 'session.json').read_text())
        if baseline.get('kind') != 'baseline': parser.error('comparison session must be a baseline')
    runtime_changes = command(['git', 'diff', 'HEAD', '--name-only', '--', '*.cpp', '*.hpp', '*.h', '*.c'])['stdout'].strip()
    if args.kind == 'baseline' and runtime_changes:
        parser.error('baseline refuses changed emulator/runtime source: ' + runtime_changes)
    manifest = json.loads(args.manifest.read_text())
    cases = [c.copy() for c in manifest['cases'] if not args.case or c['id'] in args.case]
    if not cases or set(args.case) - {c['id'] for c in cases}: parser.error('Unknown or empty case selection')
    commit = command(['git', 'rev-parse', 'HEAD'])['stdout'].strip()
    verification = None
    if args.master_verification:
        verification = json.loads(args.master_verification.read_text())
        remote = verification['remote_master']
        if verification['commit'] != commit:
            parser.error('Earlier master verification is for a different source commit')
    else:
        remote = command(['git', 'ls-remote', 'origin', 'refs/heads/master'])
    if remote.get('exit_code') != 0 or (args.kind == 'baseline' and not remote['stdout'].startswith(commit + '\t')):
        parser.error('HEAD must match verified remote master for a baseline run: ' + json.dumps(remote))
    for case in cases:
        path = Path(os.path.expanduser(case['rom']))
        if not path.is_absolute(): path = ROOT / path
        case['identity'] = rom_identity(path)
        if case['identity']['sha256'] != case['sha256']: parser.error('ROM hash mismatch: ' + case['id'])
    output = (args.output or ROOT / 'build-working/gamegear-baseline' / commit /
              time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())).resolve()
    output.mkdir(parents=True, exist_ok=False)
    emulator = ROOT / 'build-working/timeEmulator'
    binaries = [emulator, ROOT / 'build-working/libtime-sdl-frontend-plugin.so',
                ROOT / 'build-working/libtime-sdl-audio-output-plugin.so']
    metadata = {'schema': 'proto-time-gamegear-baseline-v1', 'commit': commit, 'remote_master': remote,
                'created_utc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                'remote_url': command(['git', 'remote', 'get-url', 'origin']),
                'verification_evidence': {'path': str(args.master_verification.resolve()), 'sha256': sha(args.master_verification)} if args.master_verification else None,
                'git_status': command(['git', 'status', '--short']),
                'git_diff': command(['git', 'diff', '--binary']),
                'runner_sha256': sha(__file__), 'manifest_sha256': sha(args.manifest),
                'binaries': {str(p): sha(p) for p in binaries}, 'host': platform.uname()._asdict(),
                'cpu': command(['lscpu']), 'initial_load_average': os.getloadavg(),
                'compiler': command(['c++', '--version']), 'cmake': command(['cmake', '--version']),
                'audio': command(['pactl', 'info']), 'sources': command(['pactl', 'list', 'short', 'sources']),
                'sinks': command(['pactl', '-f', 'json', 'list', 'sinks']),
                'display': {k: os.environ.get(k) for k in ('DISPLAY', 'WAYLAND_DISPLAY', 'XDG_SESSION_TYPE')},
                'parameters': vars(args) | {'manifest': str(args.manifest), 'output': str(output),
                    'baseline_session': str(args.baseline_session) if args.baseline_session else None,
                    'master_verification': str(args.master_verification) if args.master_verification else None},
                'cases': cases, 'results': [], 'kind': args.kind, 'status': 'prepared',
                'baseline_commit': baseline['commit'] if baseline else commit,
                'protocol': 'soaks-only' if args.soaks_only else 'full' if args.duration == 120 and args.repetitions == 3 and not args.skip_soaks and len(cases) == len(manifest['cases']) else 'probe/subset'}
    (output / 'CMakeCache.txt').write_bytes((ROOT / 'build-working/CMakeCache.txt').read_bytes())
    (output / 'compile_commands.json').write_bytes((ROOT / 'build-working/compile_commands.json').read_bytes())
    if args.master_verification:
        (output / 'master-verification.json').write_bytes(args.master_verification.read_bytes())
    (output / 'runner.py').write_bytes(Path(__file__).read_bytes())
    (output / 'corpus.json').write_bytes(args.manifest.read_bytes())
    write_json(output / 'human-review.json', {c['id']: {
        'reviewer': None, 'reviewed_at': None, 'rendering': 'unverified',
        'audible_output': 'unverified', 'physical_keyboard': 'unverified',
        'controller': 'unavailable', 'notes': ''} for c in cases})
    write_json(output / 'session.json', metadata)
    print(str(output), flush=True)
    if args.prepare_only: return 0
    try:
        x = XWindow()
    except (OSError, RuntimeError) as error:
        metadata['status'] = 'unavailable'
        metadata['error'] = str(error)
        write_json(output / 'session.json', metadata)
        write_matrix(output, metadata)
        return 1
    sink_info = command(['pactl', 'get-default-sink'])
    sink = sink_info.get('stdout', '').strip() if sink_info.get('exit_code') == 0 else ''
    try:
        runs = [] if args.soaks_only else [(c, str(i + 1), args.duration) for i in range(args.repetitions) for c in cases]
        if not args.skip_soaks:
            runs += [(c, 'soak', 600) for c in cases if c.get('soak')]
        if not runs: raise ValueError('No eligible soak cases selected')
        for case, index, duration in runs:
            for path, expected in metadata['binaries'].items():
                if sha(path) != expected: raise RuntimeError('Binary changed during campaign: ' + path)
            if sha(case['identity']['path']) != case['sha256']:
                raise RuntimeError('ROM changed during campaign: ' + case['id'])
            print(f'START {case["id"]} {index} {duration}s', flush=True)
            result = run_case(case, duration, index, output, emulator, x, sink)
            metadata['results'].append(result)
            metadata['status'] = 'running'
            write_json(output / 'session.json', metadata)
            write_matrix(output, metadata)
            print(f'END {case["id"]} {index} complete={result["complete"]} {result["statuses"]}', flush=True)
    except BaseException as error:
        metadata['status'] = 'interrupted'
        metadata['error'] = repr(error)
        write_json(output / 'session.json', metadata)
        write_matrix(output, metadata)
        raise
    finally:
        x.close()
    metadata['status'] = 'collected' if len(metadata['results']) == len(runs) and all(r['complete'] for r in metadata['results']) else 'incomplete'
    write_json(output / 'session.json', metadata)
    write_matrix(output, metadata)
    return 0 if metadata['status'] == 'collected' else 1


if __name__ == '__main__':
    raise SystemExit(main())
