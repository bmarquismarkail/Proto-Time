#!/usr/bin/env python3
"""Emit the repository-authored DMG analysis fixture, matching AnalysisFixture.hpp."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);args=p.parse_args()
rom=bytearray(0x8000);rom[0x100:0x103]=bytes.fromhex('c35001')
programs={0x40:'f000d9',0x150:'31f0dfcd0002cd6002cd8002cd90020e02cdb002afc4e002ccd0023e01eaffffe00ffb00c30003',
0x200:'2180031100800604f041e60328faf041e60320fa2a12130520eec9',0x260:'3ef3e0123e80e014c9',
0x280:'c3a002',0x290:'c3a002',0x2a0:'f000c9',0x2b0:'0dc8cdb002c9',0x2d0:'00c9',0x2e0:'00c9',0x300:'f00018fc',0x380:'11224488'}
for at,text in programs.items():
    data=bytes.fromhex(text);rom[at:at+len(data)]=data
args.output.write_bytes(rom)
