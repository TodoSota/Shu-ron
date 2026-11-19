"""Continuous production passes against a real generated mesh SDF."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, U, ROOT, SHADERS
from stage2 import Pipeline, metrics, shader_filename


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--regression',type=Path,required=True);p.add_argument('--mesh',type=Path,required=True)
    p.add_argument("--ssbo",action="store_true")
    p.add_argument("--linear",action="store_true")
    args=p.parse_args();dest=args.output;dest.mkdir(parents=True,exist_ok=False)
    suffix='ssbo' if args.ssbo else '3dtex'
    shader_dir=SHADERS.parent / 'ssbo' if args.ssbo else SHADERS
    plan=dict(linear=args.linear,storage=suffix,dt=.00025,steps=1200,penetration_limit=2*1.5/128,grid=128,
              friction=.6,floor=.1,rendering=False,evaluation=False,history=False,
              mesh='actual generated box SDF, translated local origin to box center',
              fixtures=['static-isotropic','translate-isotropic','rotate-isotropic','rotate-Swing'])
    (dest/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    particles=np.fromfile(args.regression/'initialization/cube.bin',np.float32).reshape(-1,48)
    physics=np.fromfile(args.regression/'initialization/physics.bin',np.float32).reshape(2,28)[0].copy()
    physics[3]=plan['dt']
    raw=np.fromfile(args.mesh,np.float32);lo,hi=raw[:3]-.5,raw[3:6]-.5
    field=np.ascontiguousarray(raw[6:].reshape(17,17,17))
    report=dict(results=[],hashes={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in
       [Path(__file__),ROOT/'tools/validation/stage2.py',args.mesh,*[shader_dir/shader_filename(s,suffix,args.linear) for s in ['setup','p2g','grid','g2p']]]})
    gl=GL(dest);report['gpu']=gl.info
    gl.ActiveTexture=c.WINFUNCTYPE(None,U)(gl.glfw.glfwGetProcAddress(b'glActiveTexture'))
    try:
        for name in plan['fixtures']:
            folder=dest/name;folder.mkdir();gl.output=folder
            gl.ActiveTexture(0x84C0);pipe=Pipeline(gl,physics,ssbo=args.ssbo,linear=args.linear);pipe.upload(particles)
            program=pipe.programs[2];gl.UseProgram(program)
            for uniform,value in [('sdf_aabb_min',lo),('sdf_aabb_max',hi)]:gl.Uniform3f(gl.GetUniformLocation(program,uniform.encode()),*value)
            tex=U();gl.GenTextures(1,c.byref(tex));gl.ActiveTexture(0x84C0+4);gl.BindTexture(0x806F,tex)
            for key in [0x2801,0x2800]:gl.TexParameteri(0x806F,key,0x2601)
            for key in [0x2802,0x2803,0x8072]:gl.TexParameteri(0x806F,key,0x812F)
            gl.TexImage3D(0x806F,0,0x822E,17,17,17,0,0x1903,0x1406,field.ctypes.data);gl.ActiveTexture(0x84C0)
            scale=np.array([.06,.4,.06]) if name=='rotate-Swing' else np.array([.2]*3)
            velocity=np.array([.02,0,0]) if name=='translate-isotropic' else np.zeros(3)
            omega=.5 if name.startswith('rotate') else 0.
            result=dict(name=name,trace=[],max_penetration=0.,contact_seen=False)
            try:
                for step in range(1,plan['steps']+1):
                    t=step*plan['dt'];angle=omega*t
                    rotation=np.array([[np.cos(angle),-np.sin(angle),0],[np.sin(angle),np.cos(angle),0],[0,0,1]])
                    center=np.array([.75,.35,.75])+velocity*t
                    model=np.eye(4);model[:3,:3]=rotation@np.diag(scale);model[:3,3]=center
                    gl.UseProgram(program)
                    for uniform,mat in [('sdf_model',model),('sdf_inv_model',np.linalg.inv(model))]:
                        data=np.ascontiguousarray(mat.T,np.float32);gl.UniformMatrix4fv(gl.GetUniformLocation(program,uniform.encode()),1,0,data.ctypes.data)
                    for uniform,value in [('sdf_velocity',velocity),('sdf_angular_velocity',[0,0,omega]),('sdf_center',center)]:gl.Uniform3f(gl.GetUniformLocation(program,uniform.encode()),*value)
                    pipe.step()
                    if step%100==0 or step==1:
                        current=pipe.read();state=metrics(current,args.linear)
                        q=abs((current[:,:3]-center)@rotation)-.5*scale
                        distance=np.linalg.norm(np.maximum(q,0),axis=1)+np.minimum(q.max(axis=1),0)
                        penetration=float(max(0,-distance.min()))
                        result['max_penetration']=max(result['max_penetration'],penetration)
                        result['contact_seen'] |= bool(distance.min()<physics[17])
                        result['trace'].append(dict(step=step,penetration=penetration,**state))
                        if not state['finite'] or state['det_min']<=0 or state['support_outside']:raise RuntimeError('Invalid particle state')
                        print(name,step,penetration,flush=True)
                current.tofile(folder/'final.bin')
                result['passed']=result['max_penetration']<=plan['penetration_limit'] and result['contact_seen']
            except Exception as error:result['passed']=False;result['error']=repr(error)
            report['results'].append(result);pipe.close();gl.DeleteTextures(1,c.byref(tex))
        report['gl_error']=int(gl.GetError());report['passed']=report['gl_error']==0 and all(x['passed'] for x in report['results'])
    finally:
        gl.close();(dest/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
