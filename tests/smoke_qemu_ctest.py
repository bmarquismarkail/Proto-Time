#!/usr/bin/env python3
"""Launcher identity regression: relocated build aliases and dlopen modules."""
import importlib.util
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runner',ROOT/'tools/qemu_ctest.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
with tempfile.TemporaryDirectory(prefix='time-qemu-launcher-') as temporary:
    root=Path(temporary);build=root/'physical';build.mkdir();alias=root/'alias';alias.symlink_to(build,target_is_directory=True)
    binary=build/'tool';header=bytearray(64);header[:6]=b'\x7fELF\x02\x01';header[16:20]=b'\x02\x00\xb7\x00';binary.write_bytes(header)
    module=build/'module.so';module.write_bytes(header)
    qemu=root/'qemu';qemu.write_bytes(b'runner');sysroot=root/'sysroot';sysroot.mkdir()
    (build/'CTestTestfile.cmake').write_text(f'add_test(cli "/usr/bin/python3" "test.py" "{alias}/tool" "{alias}/module.so")\nadd_test(direct "{qemu}" "-L" "{sysroot}" "{alias}/tool")\n')
    output=root/'shadow';runner.prepare(build.resolve(),output,qemu,sysroot)
    text=(output/'CTestTestfile.cmake').read_text()
    assert f'"{output}/bin/tool" "{alias}/module.so"' in text
    assert f'"{sysroot}" "{alias}/tool"' in text
    assert not (output/'bin/module.so').exists()
    assert binary.read_bytes()==header
print('QEMU relocated build identity and module preservation passed')
