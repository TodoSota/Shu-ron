"""Curved and nonsmooth SDF distance approximation against exact convex normal rays."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, SHADERS, U, I, P, section, read_shader


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    dest=p.parse_args().output;dest.mkdir(parents=True,exist_ok=False)
    radius=1.5/256;dx=1.5/128
    plan=dict(resolution=64,aabb=[-.8,.8],radius=radius,distance_abs=dx/8,normal_abs=.15,
              supported='smooth sphere and box face; normal/distance at gap .25*radius; strong contact tests at .5 and 1.5 radii',
              diagnostic='box edges: no unique surface normal; approximation error reported, not claimed supported')
    (dest/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    source=read_shader((SHADERS/'mpm_grid_3dtex.comp'))
    shader='''#version 430
layout(local_size_x=64) in;
layout(binding=0) uniform sampler3D sdf_tex;
uniform mat4 sdf_model,sdf_inv_model;
uniform vec3 sdf_aabb_min,sdf_aabb_max;
layout(std430,binding=4) readonly buffer Input {vec4 points[];};
layout(std430,binding=5) writeonly buffer Output {vec4 data[];};
void main(){uint i=gl_GlobalInvocationID.x;if(i>=points.length())return;
vec3 pos_local=(sdf_inv_model*points[i]).xyz;
'''+section(source,'vec3 sdf_extent =','// 6.')+section(source,'vec3 texSize =','// 8.')+'\ndata[i]=vec4(normal_world,dist_world);}'
    gl=GL(dest);report=dict(gpu=gl.info,results=[],hashes={'grid':hashlib.sha256(source.encode()).hexdigest(),'tool':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
    try:
        for name,ret,signature in [('GetUniformLocation',I,[U,c.c_char_p]),('Uniform3f',None,[I,c.c_float,c.c_float,c.c_float]),('UniformMatrix4fv',None,[I,I,U,P])]:
            setattr(gl,name,c.WINFUNCTYPE(ret,*signature)(gl.glfw.glfwGetProcAddress(('gl'+name).encode())))
        program=gl.program(shader);gl.UseProgram(program)
        for name,value in [('sdf_aabb_min',-.8),('sdf_aabb_max',.8)]:gl.Uniform3f(gl.GetUniformLocation(program,name.encode()),value,value,value)
        axis=np.linspace(-.8,.8,64,dtype=np.float32)
        z,y,x=np.meshgrid(axis,axis,axis,indexing='ij');coords=np.stack([x,y,z],axis=-1)
        tex=U();gl.GenTextures(1,c.byref(tex));gl.BindTexture(0x806F,tex)
        for key in [0x2801,0x2800]:gl.TexParameteri(0x806F,key,0x2601)
        for key in [0x2802,0x2803,0x8072]:gl.TexParameteri(0x806F,key,0x812F)
        angle=.61;r=np.array([[np.cos(angle),-np.sin(angle),0],[np.sin(angle),np.cos(angle),0],[0,0,1]])
        for geometry in ['sphere','box']:
            delta=abs(coords)-.5
            field=np.linalg.norm(coords,axis=-1)-.5 if geometry=='sphere' else np.linalg.norm(np.maximum(delta,0),axis=-1)+np.minimum(delta.max(axis=-1),0)
            field=np.ascontiguousarray(field,np.float32);gl.TexImage3D(0x806F,0,0x822E,64,64,64,0,0x1903,0x1406,field.ctypes.data)
            for transform,scale in [('isotropic',np.array([.2]*3)),('Swing',np.array([.06,.4,.06]))]:
                model=np.eye(4);model[:3,:3]=r@np.diag(scale);model[:3,3]=.75
                for name,mat in [('sdf_model',model),('sdf_inv_model',np.linalg.inv(model))]:
                    data=np.ascontiguousarray(mat.T,np.float32);gl.UniformMatrix4fv(gl.GetUniformLocation(program,name.encode()),1,0,data.ctypes.data)
                points=[];expected=[]
                for direction in [[1,0,0],[0,1,0],[0,0,1],[1,1,1],[1,.2,.4],[.2,1,.1]]:
                    direction=np.array(direction,float);direction/=np.linalg.norm(direction)
                    if geometry=='sphere':
                        surface=direction*.5;normal=r@(direction/scale);normal/=np.linalg.norm(normal);kind='smooth'
                    else:
                        if np.count_nonzero(direction)==1:
                            surface=direction*.5;normal=r@direction;kind='face'
                        else:
                            surface=np.array([.5,.5,.1]);normal=r@np.array([1,2,0])/np.sqrt(5);kind='edge'
                    world=model[:3,:3]@surface+.75
                    for factor in [.25,.5,1.,1.5,2.]:
                        points.append([*(world+normal*radius*factor),1]);expected.append((factor,normal,kind))
                points=np.array(points,np.float32);inp=gl.buffer(points,4);out=gl.buffer(np.zeros_like(points),5)
                gl.dispatch(program,len(points));actual=gl.read(out,points.shape).astype(float)
                for i,(factor,normal,kind) in enumerate(expected):
                    error=float(abs(actual[i,3]-radius*factor));normal_error=float(np.linalg.norm(actual[i,:3]-normal))
                    row=dict(geometry=geometry,transform=transform,kind=kind,gap_radii=factor,distance_error=error,
                             normal_error=normal_error,estimated_distance=float(actual[i,3]),finite=bool(np.isfinite(actual[i]).all()))
                    row['passed']=row['finite']
                    if kind!='edge':
                        if factor==.25:row['passed'] &= error<=plan['distance_abs'] and normal_error<=plan['normal_abs']
                        if factor==.5:row['passed'] &= actual[i,3]<radius
                        if factor>=1.5:row['passed'] &= actual[i,3]>=radius
                    row['passed']=bool(row['passed']);report['results'].append(row)
                for buffer in [inp,out]:gl.DeleteBuffers(1,c.byref(buffer))
        gl.DeleteTextures(1,c.byref(tex));gl.DeleteProgram(program)
        report['gl_error']=int(gl.GetError());report['passed']=report['gl_error']==0 and all(x['passed'] for x in report['results'])
    finally:
        gl.close();(dest/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
