#!/usr/bin/env python3
"""Independent stdio client: both cores, real stepping, RAM edits and trace output."""
import base64
import json
import os
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path

def verify(executable, core, directory):
    rom=bytearray(32768)
    program=bytes([0x3e,0x42,0x32 if core=='gamegear' else 0xea,0,0xc0,0,0x18,0xfe])
    entry=0 if core=='gamegear' else 0x100
    rom[entry:entry+len(program)]=program
    path=directory/(core+'.rom');path.write_bytes(rom)
    trace=directory/(core+'.jsonl')
    process=subprocess.Popen([str(executable),'--core',core,'--rom',str(path),'--trace',str(trace)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    pending=b''; sequence=0
    def receive():
        nonlocal pending
        deadline=time.monotonic()+20
        while True:
            if b'\r\n\r\n' in pending:
                header,body=pending.split(b'\r\n\r\n',1)
                count=int(header.removeprefix(b'Content-Length: '))
                if len(body)>=count:
                    result=json.loads(body[:count]);pending=body[count:];return result
            timeout=deadline-time.monotonic()
            if timeout<=0 or not select.select([process.stdout],[],[],timeout)[0]:raise AssertionError('DAP response timeout')
            chunk=os.read(process.stdout.fileno(),4096)
            if not chunk:raise AssertionError('DAP disconnected: '+process.stderr.read().decode())
            pending+=chunk
    def request(command,arguments=None):
        nonlocal sequence
        sequence+=1;body=json.dumps({'seq':sequence,'type':'request','command':command,'arguments':arguments or {}}).encode()
        process.stdin.write(b'Content-Length: '+str(len(body)).encode()+b'\r\n\r\n'+body);process.stdin.flush()
        return sequence
    def response(seq):
        while True:
            value=receive()
            if value['type']=='response' and value['request_seq']==seq:return value
    try:
        assert response(request('initialize'))['success']
        attach=request('attach');assert receive()['event']=='initialized'
        assert response(request('configurationDone'))['success'];assert response(attach)['success'];assert receive()['event']=='stopped'
        assert response(request('setInstructionBreakpoints',{'breakpoints':[{'instructionReference':hex(entry+2)}]}))['success']
        assert response(request('continue',{'threadId':1}))['success']
        while (stopped:=receive()).get('event')!='stopped':pass
        assert stopped['body']['reason']=='breakpoint'
        stack=response(request('stackTrace',{'threadId':1}))['body']['stackFrames'][0]
        assert int(stack['instructionPointerReference'],16)==entry+2
        scopes=response(request('scopes',{'frameId':stack['id']}))['body']['scopes'];scope=scopes[0]['variablesReference']
        variables=response(request('variables',{'variablesReference':scope}))['body']['variables']
        assert any(v['name']=='AF' and v['value'].startswith('0x42') for v in variables)
        assert response(request('writeMemory',{'memoryReference':'0xc001','data':base64.b64encode(b'\x99').decode()}))['success']
        assert not response(request('variables',{'variablesReference':scope}))['success']
        memory=response(request('readMemory',{'memoryReference':'0xe001','count':1}))
        assert base64.b64decode(memory['body']['data'])==b'\x99'
        assert not response(request('writeMemory',{'memoryReference':'0x8000','data':'AQ=='}))['success']
        assert response(request('setInstructionBreakpoints',{'breakpoints':[]}))['success']
        assert response(request('setDataBreakpoints',{'breakpoints':[{'dataId':'memory:49152','accessType':'write'}]}))['success']
        assert response(request('continue',{'threadId':1}))['success']
        while (stopped:=receive()).get('event')!='stopped':pass
        assert stopped['body']['reason']=='data breakpoint'
        memory=response(request('readMemory',{'memoryReference':'0xc000','count':1}))
        assert base64.b64decode(memory['body']['data'])==b'\x42'
        assert response(request('disconnect'))['success'];process.stdin.close();assert process.wait(timeout=20)==0
        records=[json.loads(line) for line in trace.read_text().splitlines()]
        assert records[0]['core']==core and records[0]['romSha256']
        assert any(r.get('kind')==2 for r in records)
        assert any(r.get('kind')=='profile' and r['complete'] for r in records)
        assert records[-1]['kind']=='footer' and records[-1]['complete']
    finally:
        if process.poll() is None:process.kill();process.wait()

with tempfile.TemporaryDirectory(prefix='time-dap-cli-') as temporary:
    for core in ['gameboy','gamegear']:verify(Path(sys.argv[1]).resolve(),core,Path(temporary))

# Malformed framing disconnects cleanly and cannot leave the machine thread alive.
with tempfile.TemporaryDirectory(prefix='time-dap-reject-') as temporary:
    rom=Path(temporary)/'test.gb';rom.write_bytes(bytes(32768))
    run=subprocess.run([sys.argv[1],'--rom',str(rom)],input=b'Content-Length: -1\r\n\r\n{}',capture_output=True,timeout=20)
    assert run.returncode==1 and b'invalid integer' in run.stderr
