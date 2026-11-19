"""Synthetic isotropic SDF plane: verify grid contact threshold uses p_radius."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, ROOT, SHADERS, U
from stage2 import Pipeline


def main():
    p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    radius=1.5/256
    fixtures=[('inside',.5*radius,radius,[-1,0,0],[0,0,0]),
              ('outside',1.5*radius,radius,[-1,0,0],[-1,0,0]),
              ('smaller-radius',.5*radius,.25*radius,[-1,0,0],[-1,0,0]),
              ('separating',.5*radius,radius,[1,0,0],[1,0,0]),
              ('friction',.5*radius,radius,[-1,2,0],[0,1.4,0]),
              ('zero-gradient',0,radius,[-1,2,0],[-1,2,0])]
    (args.output/'plan.json').write_text(json.dumps(dict(fixtures=fixtures,tolerance=2e-6,
         sdf='16^3 exact plane x - (.75-distance), identity model, AABB [0,1.5]^3',
         floor=False,gravity=False),indent=2),encoding='utf-8')
    grid_shader=(SHADERS.parent/'ssbo/mpm_grid_ssbo.comp' if args.ssbo else SHADERS/'mpm_grid_3dtex.comp')
    report=dict(results=[],hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                                 for f in [Path(__file__),ROOT/'tools/validation/stage2.py',grid_shader]})
    gl=GL(args.output); report['gpu']=gl.info
    try:
        gl.ActiveTexture=c.WINFUNCTYPE(None,U)(gl.glfw.glfwGetProcAddress(b'glActiveTexture'))
        for name,distance,r,v,expected in fixtures:
            physics=np.zeros(28,np.float32); physics[5]=1; physics[7]=-10
            physics[9:12]=.6,1.5/128,128/1.5; physics[17]=r
            pipe=Pipeline(gl,physics,ssbo=args.ssbo); grid=pipe.programs[2]; gl.UseProgram(grid)
            for uniform,value in [('sdf_aabb_min',0),('sdf_aabb_max',1.5)]:
                gl.Uniform3f(gl.GetUniformLocation(grid,uniform.encode()),value,value,value)
            tex=U(); gl.GenTextures(1,c.byref(tex)); gl.ActiveTexture(0x84C0+4); gl.BindTexture(0x806F,tex)
            gl.TexParameteri(0x806F,0x2801,0x2601); gl.TexParameteri(0x806F,0x2800,0x2601)
            sdf=np.broadcast_to(np.linspace(0,1.5,16,dtype=np.float32)-(.75-distance),(16,16,16)).copy()
            if name=='zero-gradient': sdf[:]=-1
            gl.TexImage3D(0x806F,0,0x822E,16,16,16,0,0x1903,0x1406,sdf.ctypes.data)
            fields=np.zeros((4,128,128,128),np.float32); fields[:3,64,64,64]=v; fields[3,64,64,64]=1
            gl.ActiveTexture(0x84C0)
            pipe.upload_grid(fields)
            gl.UseProgram(pipe.programs[2])
            gl.DispatchCompute(16,16,16)
            actual=pipe.read_grid()[:3,64,64,64].tolist()
            error=float(np.max(abs(np.array(actual)-expected)))
            report['results'].append(dict(name=name,actual=actual,error=error,passed=error<=2e-6))
            print(name,actual,error,flush=True)
            pipe.close(); gl.DeleteTextures(1,c.byref(tex))
        report['gl_error']=int(gl.GetError())
        report['passed']=all(x['passed'] for x in report['results']) and report['gl_error']==0
    finally:
        gl.close(); (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
