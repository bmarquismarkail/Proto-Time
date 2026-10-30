'use strict';
// Separately versioned, owned-frame WebGL2 presentation contract. No machine
// object, interpreter, network callback or guest mutation is accepted here.
class TimePostProcessV1 {
    static version=1;
    static defaultSource=`#version 300 es
precision mediump float;
uniform sampler2D uFrame;
in vec2 vUV;
out vec4 outColor;
void main() {
    vec4 pixel = texture(uFrame, vUV);
    float stripe = mod(floor(gl_FragCoord.y), 2.0) == 0.0 ? 0.82 : 1.0;
    outColor = vec4(pixel.rgb * stripe, pixel.a);
}`;
    constructor(canvas) {
        this.canvas=canvas;this.gl=canvas.getContext('webgl2',{alpha:false,antialias:false,depth:false,stencil:false,preserveDrawingBuffer:true});
        if(!this.gl)throw Error('WebGL2 is unavailable');
        const renderer=this.gl.getExtension('WEBGL_debug_renderer_info');
        this.renderer=String(this.gl.getParameter(renderer?renderer.UNMASKED_RENDERER_WEBGL:this.gl.RENDERER));
        this.program=null;this.texture=null;this.source='';this.binding=null;this.lost=false;this.error='';this.bytes=new Uint8Array(160*144*4);
        this.onLost=e=>{e.preventDefault();this.lost=true;this.program=null;this.texture=null;this.error='GPU context lost; canonical display remains available';};
        this.onRestored=()=>{this.lost=false;try{this.initialize();if(this.source)this.apply(this.source,this.binding);}catch(e){this.error=e.message;}};
        canvas.addEventListener('webglcontextlost',this.onLost);canvas.addEventListener('webglcontextrestored',this.onRestored);this.initialize();
    }
    initialize() {
        const gl=this.gl;this.texture=gl.createTexture();if(!this.texture)throw Error('GPU texture allocation failed');
        gl.bindTexture(gl.TEXTURE_2D,this.texture);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
        this.canvas.width=160;this.canvas.height=144;
    }
    static identity(binding) {
        if(!binding||!['gameboy','gamegear'].includes(binding.core)||!/^[0-9a-f]{64}$/.test(binding.romSha256)||!/^\d{1,20}$/.test(binding.generation))throw Error('Invalid shader capture binding');
        if(BigInt(binding.generation)>18446744073709551615n)throw Error('Invalid shader capture identity');
        return JSON.stringify([binding.core,binding.romSha256,binding.generation]);
    }
    apply(source,binding) {
        const identity=TimePostProcessV1.identity(binding);
        if(this.lost)throw Error('GPU context is lost');
        if(typeof source!=='string'||!source.length||new TextEncoder().encode(source).length>16384)throw Error('Shader source budget is 16 KiB');
        const gl=this.gl,shaders=[];let candidate=null;
        try {
            for(const [type,text] of [[gl.VERTEX_SHADER,`#version 300 es
out vec2 vUV;
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);vUV=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0.0,1.0);}`],[gl.FRAGMENT_SHADER,source]]) {
                const shader=gl.createShader(type);if(!shader)throw Error('Shader allocation failed');shaders.push(shader);gl.shaderSource(shader,text);gl.compileShader(shader);
                if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error((gl.getShaderInfoLog(shader)||'Shader compile failed').slice(0,2048));
            }
            candidate=gl.createProgram();if(!candidate)throw Error('Shader program allocation failed');
            for(const shader of shaders)gl.attachShader(candidate,shader);gl.linkProgram(candidate);
            if(!gl.getProgramParameter(candidate,gl.LINK_STATUS))throw Error((gl.getProgramInfoLog(candidate)||'Shader link failed').slice(0,2048));
            const old=this.program;this.program=candidate;candidate=null;this.source=source;this.binding={...binding};this.identity=identity;this.error='';if(old)gl.deleteProgram(old);
        }catch(e){this.error=e.message;throw e;}finally{if(candidate)gl.deleteProgram(candidate);for(const shader of shaders)gl.deleteShader(shader);}
    }
    draw(rgba,binding) {
        if(this.lost||!this.program)return false;
        if(TimePostProcessV1.identity(binding)!==this.identity){this.error='Capture generation changed; apply the shader again';return false;}
        if(typeof rgba!=='string'||rgba.length!==160*144*8||/[^0-9a-f]/i.test(rgba))throw Error('Invalid owned shader frame');
        for(let i=0;i<this.bytes.length;++i)this.bytes[i]=parseInt(rgba.slice(i*2,i*2+2),16);
        const gl=this.gl;gl.viewport(0,0,160,144);gl.useProgram(this.program);gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,this.texture);
        gl.pixelStorei(gl.UNPACK_ALIGNMENT,1);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,160,144,0,gl.RGBA,gl.UNSIGNED_BYTE,this.bytes);
        gl.uniform1i(gl.getUniformLocation(this.program,'uFrame'),0);gl.drawArrays(gl.TRIANGLES,0,3);
        if(gl.isContextLost()){this.error='GPU context lost';return false;}return true;
    }
    dispose() {
        this.canvas.removeEventListener('webglcontextlost',this.onLost);this.canvas.removeEventListener('webglcontextrestored',this.onRestored);
        if(!this.lost){if(this.program)this.gl.deleteProgram(this.program);if(this.texture)this.gl.deleteTexture(this.texture);}this.program=null;this.texture=null;
    }
}
if(typeof module!=='undefined')module.exports=TimePostProcessV1;

class TimeAnnotationsV1 {
    constructor(){this.labels=new Map();this.binding='';}
    apply(document,meta,snapshot) {
        if(new TextEncoder().encode(JSON.stringify(document)).length>65536||document.schemaVersion!==1||document.core!==meta.core||document.romSha256!==meta.romSha256||
           document.generation!==snapshot.videoGeneration||!/^\d{1,20}$/.test(document.videoSequence)||!Array.isArray(document.labels)||document.labels.length>256)throw Error('Annotation capture binding or budget rejected');
        const observed=new Set(snapshot.resources.map(r=>r.kind+':'+r.sourceHash));const staged=new Map();
        for(const label of document.labels) {
            const key=label.kind+':'+label.sourceHash;
            if(!observed.has(key)||staged.has(key)||label.status!=='reviewed-annotation')throw Error('Unknown, duplicate or unreviewed label');
            for(const [field,max] of [['label',128],['reviewer',128],['evidence',1024]])if(typeof label[field]!=='string'||!label[field].length||label[field].length>max||label[field].includes('\0'))throw Error('Invalid annotation text');
            staged.set(key,Object.freeze({...label}));
        }
        this.labels=staged;this.binding=TimePostProcessV1.identity({core:meta.core,romSha256:meta.romSha256,generation:document.generation});
    }
    lookup(resource,meta,snapshot) {
        if(TimePostProcessV1.identity({core:meta.core,romSha256:meta.romSha256,generation:snapshot.videoGeneration})!==this.binding)return null;
        return this.labels.get(resource.kind+':'+resource.sourceHash)||null;
    }
}
if(typeof module!=='undefined')module.exports.Annotations=TimeAnnotationsV1;
