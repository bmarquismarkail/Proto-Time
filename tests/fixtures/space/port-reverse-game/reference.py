#!/usr/bin/env python3
"""Independent libretro process. Does not import/link T.I.M.E. execution code."""
import argparse,ctypes as C,hashlib,json,math,struct,wave,zlib
from pathlib import Path
sha=lambda b:hashlib.sha256(b).hexdigest()
class Info(C.Structure):_fields_=[('name',C.c_char_p),('version',C.c_char_p),('extensions',C.c_char_p),('fullpath',C.c_bool),('extract',C.c_bool)]
class Game(C.Structure):_fields_=[('path',C.c_char_p),('data',C.c_void_p),('size',C.c_size_t),('meta',C.c_char_p)]
class Variable(C.Structure):_fields_=[('key',C.c_char_p),('value',C.c_char_p)]
class Geometry(C.Structure):_fields_=[('width',C.c_uint),('height',C.c_uint),('maxwidth',C.c_uint),('maxheight',C.c_uint),('aspect',C.c_float)]
class Timing(C.Structure):_fields_=[('fps',C.c_double),('sample_rate',C.c_double)]
class AV(C.Structure):_fields_=[('geometry',Geometry),('timing',Timing)]
def png(path,w,h,rgb):
    def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
    raw=b''.join(b'\0'+rgb[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
def run(core,rom,out,commands):
    out.mkdir(parents=True,exist_ok=True)
    lib=C.CDLL(str(core));mask=0;fmt=0;video=None;samples=[];options={};option_bytes={};served_options={};audio_frame=[]
    ENV=C.CFUNCTYPE(C.c_bool,C.c_uint,C.c_void_p)
    VIDEO=C.CFUNCTYPE(None,C.c_void_p,C.c_uint,C.c_uint,C.c_size_t)
    SAMPLE=C.CFUNCTYPE(None,C.c_int16,C.c_int16)
    BATCH=C.CFUNCTYPE(C.c_size_t,C.POINTER(C.c_int16),C.c_size_t)
    POLL=C.CFUNCTYPE(None);INPUT=C.CFUNCTYPE(C.c_int16,C.c_uint,C.c_uint,C.c_uint,C.c_uint)
    directories=[str(out.resolve()).encode()]
    @ENV
    def env(cmd,data):
        nonlocal fmt
        cmd&=0xffff
        if cmd==10:fmt=C.cast(data,C.POINTER(C.c_int))[0];return fmt in (0,1,2)
        if cmd in (9,30,31):C.cast(data,C.POINTER(C.c_char_p))[0]=directories[0];return True
        if cmd==15:
            v=C.cast(data,C.POINTER(Variable)).contents;key=v.key.decode()
            overrides={'gambatte_gb_colorization':'disabled','gambatte_gb_internal_palette':'GB - Pocket','gambatte_up_down_allowed':'enabled','genesis_plus_gx_gg_extra':'disabled','genesis_plus_gx_overscan':'disabled'}
            value=overrides.get(key,options.get(key))
            if value is None:return False
            served_options[key]=value;option_bytes[key]=value.encode();v.value=option_bytes[key];return True
        if cmd==16:
            array=C.cast(data,C.POINTER(Variable));i=0
            while array[i].key:
                key=array[i].key.decode();text=array[i].value.decode();options[key]=text.split('; ',1)[-1].split('|')[0];i+=1
            return True
        if cmd==3:C.cast(data,C.POINTER(C.c_bool))[0]=True;return True
        if cmd==17:C.cast(data,C.POINTER(C.c_bool))[0]=False;return True
        if cmd==18:C.cast(data,C.POINTER(C.c_bool))[0]=False;return True
        if cmd==24:C.cast(data,C.POINTER(C.c_uint64))[0]=2;return True
        if cmd in (11,12,35,37):return True
        return False
    @VIDEO
    def vid(data,w,h,pitch):
        nonlocal video
        if not data:return
        raw=C.string_at(data,pitch*h);rgb=bytearray()
        for y in range(h):
            for x in range(w):
                if fmt==1:
                    v=struct.unpack_from('<I',raw,y*pitch+x*4)[0];rgb.extend([(v>>16)&255,(v>>8)&255,v&255])
                else:
                    v=struct.unpack_from('<H',raw,y*pitch+x*2)[0]
                    if fmt==2:rgb.extend([((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31])
                    else:rgb.extend([((v>>10)&31)*255//31,((v>>5)&31)*255//31,(v&31)*255//31])
        video=(w,h,bytes(rgb))
    @SAMPLE
    def sample(l,r):samples.extend([l,r]);audio_frame.extend([l,r])
    @BATCH
    def batch(data,n):
        vals=list(C.cast(data,C.POINTER(C.c_int16*(n*2))).contents);samples.extend(vals);audio_frame.extend(vals);return n
    @POLL
    def poll():pass
    @INPUT
    def inp(port,device,index,id):
        if port!=0:return 0
        mapping={7:1,6:2,4:4,5:8,8:16,0:32,2:64,3:128}
        return int(bool(mask&mapping.get(id,0)))
    for name,cb in [('environment',env),('video_refresh',vid),('audio_sample',sample),('audio_sample_batch',batch),('input_poll',poll),('input_state',inp)]:
        getattr(lib,'retro_set_'+name).argtypes=[type(cb)];getattr(lib,'retro_set_'+name)(cb)
    lib.retro_init();info=Info();lib.retro_get_system_info(C.byref(info));av=AV()
    data=rom.read_bytes();buffer=C.create_string_buffer(data);game=Game(str(rom.resolve()).encode(),C.cast(buffer,C.c_void_p),len(data),None)
    lib.retro_load_game.argtypes=[C.POINTER(Game)];lib.retro_load_game.restype=C.c_bool
    if not lib.retro_load_game(C.byref(game)):raise RuntimeError('reference core rejected ROM')
    lib.retro_get_system_av_info(C.byref(av));lib.retro_get_memory_data.argtypes=[C.c_uint];lib.retro_get_memory_data.restype=C.c_void_p
    lib.retro_get_memory_size.argtypes=[C.c_uint];lib.retro_get_memory_size.restype=C.c_size_t
    def ram():
        p=lib.retro_get_memory_data(2);n=lib.retro_get_memory_size(2)
        if not p or n<0x25:raise RuntimeError('reference core lacks system RAM probes')
        return C.string_at(p,0x25)
    def state():
        b=ram();return dict(zip(['x','y','picked','score','won','pose','phase','input','previous','cue','remaining','note','ready','flag'],b[:14]))|{'frame':int.from_bytes(b[14:16],'little'),'commit':int.from_bytes(b[16:18],'little'),'bank':b[18],'blocked':b[32],'collected':b[33],'victory':b[34],'lastCue':b[35],'sound':b[36]}
    def step():
        audio_frame.clear();lib.retro_run()
    for _ in range(1200):
        step()
        if state()['ready'] and state()['commit']>=2:break
    else:raise RuntimeError('ROM did not reach ready state')
    rows=[];frame_audio=[]
    for cmd in commands:
        mask=cmd.get('mask',0)
        for _ in range(cmd['frames']):
            step();rows.append(state());frame_audio.append(list(audio_frame))
        if video:
            w,h,rgb=video;png(out/(cmd['name']+'.png'),w,h,rgb)
    with wave.open(str(out/'audio.wav'),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(round(av.timing.sample_rate));f.writeframes(struct.pack('<'+'h'*len(samples),*samples))
    result={'schemaVersion':1,'core':{'name':info.name.decode(),'version':info.version.decode(),'sha256':sha(core.read_bytes())},'romSha256':sha(data),'fps':av.timing.fps,'sampleRate':av.timing.sample_rate,'states':rows,'commands':commands,'audioFrames':frame_audio,'options':served_options,'video':list(video[:2]) if video else None,'status':'captured' if video else 'failed'}
    (out/'result.json').write_text(json.dumps(result));lib.retro_unload_game();lib.retro_deinit();return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--core',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--commands',type=Path,required=True);a=p.parse_args();run(a.core,a.rom,a.output,json.loads(a.commands.read_text()))
