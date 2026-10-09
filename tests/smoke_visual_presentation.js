'use strict';
const assert=require('node:assert/strict');
const Pipeline=require('../machine/plugins/video/remote/postprocess.js');
function fixture(){
    let serial=0;const deleted=[],draws=[],listeners=new Map();
    const gl={VERTEX_SHADER:1,FRAGMENT_SHADER:2,COMPILE_STATUS:3,LINK_STATUS:4,TEXTURE_2D:5,
        getExtension:()=>null,getParameter:()=> 'fixture renderer',createTexture:()=>({id:++serial}),bindTexture(){},texParameteri(){},
        createShader:type=>({id:++serial,type}),shaderSource:(s,text)=>s.source=text,compileShader(){},
        getShaderParameter:s=>s.source!=='invalid',getShaderInfoLog:()=> 'compile rejected',deleteShader:s=>deleted.push(s),
        createProgram:()=>({id:++serial}),attachShader(){},linkProgram(){},getProgramParameter:()=>!gl.rejectLink,
        getProgramInfoLog:()=> 'link rejected',deleteProgram:p=>deleted.push(p),deleteTexture:p=>deleted.push(p),
        viewport(){},useProgram:p=>draws.push(p),activeTexture(){},pixelStorei(){},texImage2D(){},uniform1i(){},
        getUniformLocation:()=>null,drawArrays(){},isContextLost:()=>false};
    const canvas={getContext:()=>gl,addEventListener:(key,value)=>listeners.set(key,value),removeEventListener:key=>listeners.delete(key)};
    return {gl,canvas,listeners,deleted,draws};
}
for(const core of ['gameboy','gamegear']){
    const f=fixture(),p=new Pipeline(f.canvas),binding={core,romSha256:'a'.repeat(64),generation:'2'};
    p.apply(Pipeline.defaultSource,binding);const first=p.program;
    assert.throws(()=>p.apply('invalid',binding),/compile rejected/);assert.equal(p.program,first);assert(!f.deleted.includes(first));
    f.gl.rejectLink=true;assert.throws(()=>p.apply(Pipeline.defaultSource,binding),/link rejected/);assert.equal(p.program,first);f.gl.rejectLink=false;
    assert.throws(()=>p.apply('x'.repeat(16385),binding),/budget/);assert.equal(p.program,first);
    assert.throws(()=>p.apply(Pipeline.defaultSource,{...binding,core:'new-console'}),/binding/);
    const rgba='112233ff'.repeat(160*144);assert(p.draw(rgba,binding));assert.deepEqual([...p.bytes.subarray(0,4)],[17,34,51,255]);
    assert(!p.draw(rgba,{...binding,generation:'3'}));assert.match(p.error,/generation changed/);assert.throws(()=>p.draw('00',binding),/frame/);
    f.listeners.get('webglcontextlost')({preventDefault(){}});assert(!p.draw(rgba,binding));assert.throws(()=>p.apply(Pipeline.defaultSource,binding),/lost/);
    f.listeners.get('webglcontextrestored')();assert(p.draw(rgba,binding));assert.notEqual(p.program,first);
    const annotations=new Pipeline.Annotations(),resource={kind:'Tile',sourceHash:'0x0123456789abcdef'};
    const snapshot={videoGeneration:'2',resources:[resource]},meta={core,romSha256:binding.romSha256};
    const document={schemaVersion:1,...meta,videoSequence:'9',generation:'2',labels:[{...resource,label:'hud_digit',reviewer:'fixture',evidence:'scenario counter',status:'reviewed-annotation'}]};
    annotations.apply(document,meta,snapshot);assert.equal(annotations.lookup(resource,meta,snapshot).label,'hud_digit');
    assert.throws(()=>annotations.apply({...document,romSha256:'b'.repeat(64)},meta,snapshot),/binding/);
    assert.throws(()=>annotations.apply({...document,labels:[...document.labels,...document.labels]},meta,snapshot),/duplicate/);
    assert.equal(annotations.lookup(resource,meta,snapshot).label,'hud_digit');assert.equal(annotations.lookup(resource,meta,{...snapshot,videoGeneration:'3'}),null);
    p.dispose();assert.equal(f.listeners.size,0);assert.equal(p.program,null);
}
assert.throws(()=>new Pipeline({getContext:()=>null}),/unavailable/);
console.log('Both-core presentation staging, last-good shaders, bounded frames, lifecycle and reviewed annotations passed');
