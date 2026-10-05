#!/usr/bin/env python3
"""Write the original S.P.A.C.E. Game Boy fixture; no external ROM required."""
import argparse
from pathlib import Path
p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
args = p.parse_args()
rom = bytearray(0x8000)
rom[0x100:0x103] = bytes.fromhex('c35001')
main = bytes.fromhex('31f0df3e01ea00c0cd7001fa00c03e03ea00c0c38001')
sub = bytes.fromhex('3e02ea00c0c9')
loop = bytes.fromhex('2100c07e7718fc')
rom[0x150:0x150+len(main)] = main
rom[0x170:0x170+len(sub)] = sub
rom[0x180:0x180+len(loop)] = loop
args.output.write_bytes(rom)
