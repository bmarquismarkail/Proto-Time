#!/usr/bin/env python3
"""Independent artifact consumer: whole-pack validation and publication failure."""
import json,subprocess,sys,tempfile,zlib,struct
from pathlib import Path
def png():
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',8,8,8,6,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+b'\xff'*32)*8))+chunk(b'IEND',b'')
def main():
    with tempfile.TemporaryDirectory(prefix='time-authoring-cli-') as directory:
        root=Path(directory);(root/'tile.png').write_bytes(png())
        for core in ('gameboy','gamegear'):
            capture={'schemaVersion':1,'core':core,'romSha256':'a'*64,'videoSequence':'9','videoGeneration':'2','resources':[{'kind':'Tile','sourceHash':'0x0123456789abcdef'}]}
            source={'schemaVersion':1,'core':core,'romSha256':'a'*64,'videoSequence':'9','generation':'2','id':'fixture','name':'Authored fixture','resources':[{'kind':'Tile','sourceHash':'0x0123456789abcdef','label':'hud_digit','reviewer':'fixture','evidence':'counter scenario','replace':{'animation':{'frames':['tile.png','tile.png'],'frameDuration':16},'transform':{'flipX':True,'rotate':90}}}]}
            cap=root/(core+'-capture.json');recipe=root/(core+'-recipe.json');output=root/(core+'-pack');cap.write_text(json.dumps(capture));recipe.write_text(json.dumps(source))
            command=[sys.argv[1],'--capture',str(cap),'--source',str(recipe),'--assets',str(root),'--output',str(output)]
            result=subprocess.run(command,capture_output=True,text=True,timeout=30);assert result.returncode==0,(result.stdout,result.stderr)
            manifest=json.loads((output/'manifest.json').read_text());labels=json.loads((output/'annotations.json').read_text())
            assert manifest['schemaVersion']==1 and manifest['target']==core and labels['romSha256']==capture['romSha256']
            assert labels['labels'][0]['status']=='reviewed-annotation'
            for frame in manifest['rules'][0]['replace']['animation']['frames']:assert (output/frame).read_bytes()==png()
            original=(output/'manifest.json').read_bytes();assert subprocess.run(command,capture_output=True,timeout=30).returncode!=0
            assert (output/'manifest.json').read_bytes()==original
            rejected=root/(core+'-rejected');command[-1]=str(rejected);source['generation']='3';recipe.write_text(json.dumps(source))
            assert subprocess.run(command,capture_output=True,timeout=30).returncode!=0 and not rejected.exists()
            recipe.write_text('['*100+'0'+']'*100);assert subprocess.run(command,capture_output=True,timeout=30).returncode!=0 and not rejected.exists()
    print('both-core CLI artifacts, animation assets, reviewed annotations, legacy schema and failed-publication isolation passed')
if __name__=='__main__':main()
