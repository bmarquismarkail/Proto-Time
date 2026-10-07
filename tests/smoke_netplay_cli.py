#!/usr/bin/env python3
"""Two independent emulator processes, a real UDP loopback transport, both cores."""
import json, os, socket, subprocess, sys, tempfile
from pathlib import Path

def ports():
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as first,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as second:
        first.bind(('127.0.0.1',0));second.bind(('127.0.0.1',0))
        return first.getsockname()[1],second.getsockname()[1]

def main():
    binary=sys.argv[1]
    with tempfile.TemporaryDirectory(prefix='time-netplay-') as directory:
        for core in ('gameboy','gamegear'):
            rom=bytearray(32768)
            if core=='gameboy':rom[0x100:0x10c]=bytes([0x3e,0x10,0xe0,0,0xf0,0,0xea,0,0xc0,0xc3,4,1])
            else:rom[:8]=bytes([0xdb,0xdc,0x32,0,0xc0,0xc3,0,0])
            source=Path(directory)/(core+'.rom');source.write_bytes(rom)
            first,second=ports();children=[]
            common=[binary,'--core',core,'--rom',str(source),'--session','01'+'00'*31,'--frames','2','--timeout-ms','60000']
            try:
                for peer,local,remote,mask in ((0,first,second,17),(1,second,first,0)):
                    children.append(subprocess.Popen(common+['--peer',str(peer),'--bind-port',str(local),'--peer-port',str(remote),'--input',str(mask)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True))
                reports=[]
                for child in children:
                    out,error=child.communicate(timeout=120)
                    assert child.returncode==0,(core,child.returncode,out,error)
                    reports.append(json.loads(out))
                assert reports[0]['finalFingerprint']==reports[1]['finalFingerprint']
                assert reports[0]['frames']==reports[1]['frames']
                assert reports[0]['configurationSha256']==reports[1]['configurationSha256']
                assert all(r['status']=='passed' and r['transport']['received']>=3 for r in reports)
            finally:
                for child in children:
                    if child.poll() is None:child.kill()
                    child.wait()
        # No peer is an explicit pause, not speculative execution or success.
        first,second=ports()
        failed=subprocess.run([binary,'--core','gamegear','--rom',str(source),'--session','01'+'00'*31,
            '--peer','0','--bind-port',str(first),'--peer-port',str(second),'--frames','1','--timeout-ms','100'],capture_output=True,text=True,timeout=30)
        assert failed.returncode!=0 and json.loads(failed.stderr)['status']=='paused',failed
    print('both-core UDP frame lockstep and final-state acknowledgment passed; absent peer paused')
if __name__=='__main__':main()
