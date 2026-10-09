#!/usr/bin/env python3
"""Independent HTTP client; real core stepping, views, rejected edits and limits."""
import hashlib,json,os,select,socket,subprocess,sys,tempfile,time,urllib.request,urllib.error
from pathlib import Path

def verify(binary,core,directory):
    rom=bytearray(32768);entry=0x100 if core=='gameboy' else 0
    code=bytes([0x3e,0x42,0xea if core=='gameboy' else 0x32,0,0xc0,0x18,0xfe])
    rom[entry:entry+len(code)]=code;path=directory/(core+'.rom');path.write_bytes(rom)
    process=subprocess.Popen([binary,'--core',core,'--rom',str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        assert select.select([process.stdout],[],[],30)[0],'browser startup timeout'
        line=process.stdout.readline();assert line,(process.poll(),process.stderr.read())
        started=json.loads(line);url=started['url'];port=int(url.rsplit(':',1)[1]);host=f'127.0.0.1:{port}'
        def get(path):
            with urllib.request.urlopen(url+path,timeout=15) as reply:return json.load(reply)
        meta=get('/api/meta');assert meta['core']==core and meta['romSha256']==hashlib.sha256(rom).hexdigest()
        assert len(meta['sourceSha256'])==64 and len(meta['token'])==64 and meta['backend']=='baseline'
        def state(predicate=lambda x:True):
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                try:current=get('/api/state')
                except (ConnectionError,urllib.error.URLError):
                    time.sleep(.02);continue
                if 'cpu' in current and predicate(current):return current
                time.sleep(.02)
            raise AssertionError('owned snapshot timeout')
        current=state();first_sequence=int(current['sequence']);time.sleep(.08)
        unchanged=state();assert int(unchanged['sequence'])-first_sequence<=1,'paused session continuously rebuilt unchanged video'
        assert int(current['videoSequence'])<=int(current['sequence']) and current['videoGeneration'].isdigit() and current['videoPauseId'].isdigit()
        assert current['cpu']['state']==1 and current['cpu']['registers'][5]==entry
        assert current['atlas']['tileCount']==(384 if core=='gameboy' else 512)
        assert len(current['frame']['rgba'])==160*144*8 and len(current['semantics'])==160*144
        assert len(current['vram'])==(8192 if core=='gameboy' else 16384)
        assert len(current['sprites'])==(160 if core=='gameboy' else 64)
        for asset,mime in (('/','text/html'),('/inspector.js','text/javascript'),('/inspector.css','text/css'),('/postprocess.js','text/javascript')):
            with urllib.request.urlopen(url+asset,timeout=10) as reply:
                assert reply.status==200 and mime in reply.headers['Content-Type']
                assert 'frame-ancestors' in reply.headers['Content-Security-Policy'] and len(reply.read())>100
        def command(operation,source=None,**extra):
            base=(source or state())['cpu'];data=json.dumps(dict(operation=operation,generation=base['generation'],pauseId=base['pauseId'],**extra)).encode()
            request=urllib.request.Request(url+'/api/command',data=data,headers={'Content-Type':'application/json','X-Time-Token':meta['token']})
            with urllib.request.urlopen(request,timeout=20) as reply:return json.load(reply)
        assert command('step',steps=2)['error']==0
        current=state(lambda s:s['cpu']['state']==1 and s['cpu']['registers'][5]==entry+5)
        assert command('inspect',current,address=0xc000,length=1)['memory']==[0x42]
        old=current;edited=command('edit',current,bytes=[{'address':0xc001,'value':0x99}]);assert edited['error']==0
        current=state(lambda s:s['cpu']['generation']==edited['generation'])
        assert command('inspect',current,address=0xe001,length=1)['memory']==[0x99]
        assert command('edit',old,bytes=[{'address':0xc001,'value':0x10}])['error']==3
        assert command('edit',current,bytes=[{'address':0xc001,'value':1},{'address':0x8000,'value':1}])['error']==5
        assert command('inspect',current,address=0xc001,length=1)['memory']==[0x99]
        assert command('continue',current)['error']==0
        current=state(lambda s:s['cpu']['state']==2)
        assert command('inspect',current,address=0xc000,length=1)['error']==2
        # Full client slots never stop the sole machine writer.
        cycle_before=int(current['cycles']);blocked=[]
        try:
            for _ in range(8):
                client=socket.create_connection(('127.0.0.1',port),timeout=5)
                client.sendall(b'GET /api/state HTTP/1.1\r\n');blocked.append(client)
            time.sleep(.2)
        finally:
            for client in blocked:client.close()
        current=state(lambda s:int(s['cycles'])>cycle_before)
        assert command('pause',current)['error']==0;current=state(lambda s:s['cpu']['state']==1)
        def raw(headers,body=b''):
            with socket.create_connection(('127.0.0.1',port),timeout=10) as client:
                client.sendall(headers+body);client.shutdown(socket.SHUT_WR);out=b''
                while chunk:=client.recv(65536):out+=chunk
            return int(out.split(b' ',2)[1])
        assert raw(f'GET /api/meta HTTP/1.1\r\nHost: evil.test\r\n\r\n'.encode())==403
        assert raw(f'GET /api/meta HTTP/1.1\r\nHost: {host}\r\nOrigin: http://evil.test\r\n\r\n'.encode())==403
        assert raw(f'POST /api/command HTTP/1.1\r\nHost: {host}\r\nContent-Length: 4097\r\n\r\n'.encode())==413
        assert raw(f'GET /api/meta HTTP/1.1\r\nHost: {host}\r\nHost: {host}\r\n\r\n'.encode())==400
        assert raw(f'GET /api/meta HTTP/1.1\r\nHost: {host}\r\nTransfer-Encoding: chunked\r\n\r\n'.encode())==403
        assert raw(f'POST /api/command HTTP/1.1\r\nHost: {host}\r\nContent-Length: 2\r\nContent-Type: application/json\r\n\r\n'.encode(),b'{}')==403
        assert get('/api/meta')['transportRejected']>=6
        assert command('inspect',current,address=0xc001,length=1)['memory']==[0x99]
    finally:
        if process.poll() is None:process.terminate()
        try:out,error=process.communicate(timeout=30)
        except subprocess.TimeoutExpired:process.kill();out,error=process.communicate();raise AssertionError('browser shutdown timeout')
        assert process.returncode==0,(core,process.returncode,out,error)

def main():
    with tempfile.TemporaryDirectory(prefix='time-browser-test-') as directory:
        for core in ('gameboy','gamegear'):verify(sys.argv[1],core,Path(directory))
    print('both-core HTTP streaming, owned video memory, transactional controls and overload/disconnection passed')
if __name__=='__main__':main()
