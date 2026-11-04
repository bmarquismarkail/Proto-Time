#!/usr/bin/env python3
"""Isolated Genesis Plus GX captures for a pinned Game Gear baseline scenario.

Reference emulated-time checkpoints are comparison material, not assertions that
wall-time SDL screenshots reached the same instruction or scene.
"""
import argparse
import ctypes as C
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fixture_reference', ROOT / 'tests/fixtures/space/port-game/reference.py')
r = importlib.util.module_from_spec(spec); spec.loader.exec_module(r)


def run(core, case, out):
    out.mkdir(parents=True, exist_ok=False)
    lib = C.CDLL(str(core)); pixel_format = 0; video = None; keys = set(); audio = bytearray()
    input_queries = {}; input_active = {}
    options = {}; values = {}; served = {}; directory = str(out).encode()
    ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
    VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
    SAMPLE = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
    BATCH = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
    POLL = C.CFUNCTYPE(None)
    INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

    @ENV
    def env(cmd, data):
        nonlocal pixel_format
        cmd &= 0xffff
        if cmd == 10:
            pixel_format = C.cast(data, C.POINTER(C.c_int))[0]
            return pixel_format in (0, 1, 2)
        if cmd in (9, 30, 31): C.cast(data, C.POINTER(C.c_char_p))[0] = directory; return True
        if cmd == 16:
            array = C.cast(data, C.POINTER(r.Variable)); i = 0
            while array[i].key:
                options[array[i].key.decode()] = array[i].value.decode().split('; ', 1)[-1].split('|')[0]; i += 1
            return True
        if cmd == 15:
            variable = C.cast(data, C.POINTER(r.Variable)).contents; key = variable.key.decode()
            value = {'genesis_plus_gx_gg_extra': 'disabled', 'genesis_plus_gx_overscan': 'disabled'}.get(key, options.get(key))
            if value is None: return False
            served[key] = value; values[key] = value.encode(); variable.value = values[key]; return True
        if cmd in (3, 17, 18): C.cast(data, C.POINTER(C.c_bool))[0] = cmd == 3; return True
        if cmd == 24: C.cast(data, C.POINTER(C.c_uint64))[0] = 2; return True
        return cmd in (11, 12, 35, 37)

    @VIDEO
    def vid(data, width, height, pitch):
        nonlocal video
        if not data: return
        video = width, height, pitch, pixel_format, C.string_at(data, pitch * height)

    def capture(path):
        width, height, pitch, fmt, raw = video
        rgb = bytearray()
        for y in range(height):
            for x in range(width):
                if fmt == 1:
                    v = struct.unpack_from('<I', raw, y * pitch + x * 4)[0]
                    rgb.extend([(v >> 16) & 255, (v >> 8) & 255, v & 255])
                else:
                    v = struct.unpack_from('<H', raw, y * pitch + x * 2)[0]
                    if fmt == 2: rgb.extend([((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31])
                    else: rgb.extend([((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31])
        r.png(path, width, height, bytes(rgb))

    @SAMPLE
    def sample(left, right): audio.extend(struct.pack('<hh', left, right))
    @BATCH
    def batch(data, count): audio.extend(C.string_at(data, count * 4)); return count
    @POLL
    def poll(): pass
    @INPUT
    def inp(port, device, index, ident):
        # Genesis Plus GX maps retropad B/A to its two Sega buttons:
        # https://github.com/libretro/Genesis-Plus-GX/blob/master/libretro/libretro.c
        input_queries[str(ident)] = input_queries.get(str(ident), 0) + 1
        pressed = int(port == 0 and {7: 'Right', 6: 'Left', 4: 'Up', 5: 'Down', 0: 'z', 8: 'x', 2: 'BackSpace', 3: 'Return'}.get(ident) in keys)
        if pressed: input_active[str(ident)] = input_active.get(str(ident), 0) + 1
        return pressed
    for name, callback in [('environment', env), ('video_refresh', vid), ('audio_sample', sample),
                           ('audio_sample_batch', batch), ('input_poll', poll), ('input_state', inp)]:
        fn = getattr(lib, 'retro_set_' + name); fn.argtypes = [type(callback)]; fn(callback)
    lib.retro_init(); info = r.Info(); lib.retro_get_system_info(C.byref(info))
    rom = Path(case['rom']).expanduser()
    if not rom.is_absolute(): rom = ROOT / rom
    data = rom.read_bytes()
    if hashlib.sha256(data).hexdigest() != case['sha256']: raise ValueError('ROM identity mismatch')
    buffer = C.create_string_buffer(data)
    game = r.Game(str(rom).encode(), C.cast(buffer, C.c_void_p), len(data), None)
    lib.retro_load_game.argtypes = [C.POINTER(r.Game)]; lib.retro_load_game.restype = C.c_bool
    if not lib.retro_load_game(C.byref(game)): raise RuntimeError('Reference core rejected cartridge')
    lib.retro_set_controller_port_device.argtypes = [C.c_uint, C.c_uint]
    lib.retro_set_controller_port_device(0, 1)  # Explicit retropad attachment.
    av = r.AV(); lib.retro_get_system_av_info(C.byref(av))
    actions = []
    for event in case['inputs']:
        actions.extend([(event['at'], event['key'], True), (event['at'] + event['hold'], event['key'], False)])
    actions.sort()
    checkpoints = list(case['checkpoints']); results = []
    for frame in range(round(max(c['at'] for c in checkpoints) * av.timing.fps) + 2):
        seconds = frame / av.timing.fps
        while actions and actions[0][0] <= seconds:
            _, key, down = actions.pop(0)
            if down: keys.add(key)
            else: keys.discard(key)
        lib.retro_run()
        while checkpoints and checkpoints[0]['at'] <= seconds:
            checkpoint = checkpoints.pop(0)
            if video:
                capture(out / (checkpoint['name'] + '.png'))
                results.append(dict(name=checkpoint['name'], frame=frame, emulated_seconds=seconds, width=video[0], height=video[1]))
    with wave.open(str(out / 'reference.wav'), 'wb') as wav:
        wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(round(av.timing.sample_rate))
        wav.writeframes(audio)
    result = dict(scenario=case, runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), core=info.name.decode(), version=info.version.decode(), core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
                  rom_sha256=case['sha256'], fps=av.timing.fps, sample_rate=av.timing.sample_rate, options=served,
                  input_queries=input_queries, input_active=input_active, checkpoints=results, alignment='emulated time; wall-time captures require scene/phase review', status='captured')
    (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    lib.retro_unload_game(); lib.retro_deinit()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=ROOT / '.internal/docs/gamegear-baseline/corpus.json')
    parser.add_argument('--case', required=True)
    parser.add_argument('--core', type=Path, default=Path('/usr/lib/libretro/genesis_plus_gx_libretro.so'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    case = next(c for c in json.loads(args.manifest.read_text())['cases'] if c['id'] == args.case)
    run(args.core, case, args.output.resolve())
