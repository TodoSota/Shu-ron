"""Independent spectral stress -> finite-dt production P2G nodal impulse."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, ROOT, SHADERS
from stage2 import Pipeline


def main():
    p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    plan=dict(dt=.001,tolerance=5e-4,mass_relative=3e-6,
              reference='eigh(FF^T), centered piecewise quadratic basis, nodal impulse',
              cases=['identity','compression','rotated-compression','shear'])
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    gl=GL(args.output)
    shader=SHADERS.parent/"ssbo/mpm_p2g_ssbo.comp" if args.ssbo else SHADERS/"mpm_p2g_3dtex.comp"
    report=dict(gpu=gl.info,results=[],hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                                             for f in [Path(__file__),ROOT/'tools/validation/stage2.py',shader]})
    try:
        physics=np.zeros(28,np.float32); physics[3]=.001
        dx=1.5/128; physics[10:12]=dx,1/dx
        physics[13:17]=2.7e-6,50000/2.6,50000*.3/(1.3*.4),.00108
        pipe=Pipeline(gl,physics,ssbo=args.ssbo)
        theta=.61
        r=np.array([[np.cos(theta),-np.sin(theta),0],[np.sin(theta),np.cos(theta),0],[0,0,1]])
        compression=np.diag([.8,.9,.95])
        matrices=[np.eye(3),compression,r@compression,np.array([[1,.3,0],[0,1,.2],[0,0,1]])]
        for name,f in zip(plan['cases'],matrices):
            a=np.zeros((1,48),np.float32); a[0,:3]=np.array([64.1,64.7,65.2])*dx
            f4=np.eye(4); f4[:3,:3]=f; a[0,24:40]=f4.T.ravel()
            pipe.upload(a)
            gl.UseProgram(pipe.programs[0]); gl.DispatchCompute(16,16,16)
            gl.MemoryBarrier(0x2000 if args.ssbo else 0x20)
            gl.UseProgram(pipe.programs[1]); gl.DispatchCompute(1,1,1)
            fields=pipe.read_grid().astype(float)
            f=a[0,24:40].reshape(4,4).T[:3,:3].astype(float)
            eig,vec=np.linalg.eigh(f@f.T); eps=.5*np.log(eig)
            tau=vec@np.diag(2*float(physics[14])*eps+float(physics[15])*sum(eps))@vec.T
            x=a[0,:3].astype(float); base=np.floor(x/dx-.5).astype(int)
            actual=[]; expected=[]
            for offset in np.ndindex(3,3,3):
                node=base+offset; d=node*dx-x; rr=np.abs(d/dx)
                w=np.prod(np.where(rr<.5,.75-rr*rr,np.where(rr<1.5,.5*(1.5-rr)**2,0)))
                impulse=-float(physics[3])*float(physics[13])*4/dx**2*w*(tau@d)
                actual.append([fields[i][node[2],node[1],node[0]] for i in range(3)])
                expected.append(impulse)
            err=float(np.linalg.norm(np.array(actual)-expected)/max(1e-12,np.linalg.norm(expected)))
            mass_error=abs(fields[3].sum()-float(physics[16]))/float(physics[16])
            # Identity has exactly zero stress; use absolute norm for the zero case.
            if name=='identity': err=float(np.linalg.norm(actual))
            result=dict(name=name,impulse_error=err,mass_error=float(mass_error),
                        passed=bool(err<=plan['tolerance'] and mass_error<=plan['mass_relative']))
            report['results'].append(result); print(json.dumps(result),flush=True)
            for buf in pipe.buffers: gl.DeleteBuffers(1,c.byref(buf))
            pipe.buffers=[]
        pipe.close(); report['gl_error']=int(gl.GetError())
        report['passed']=all(x['passed'] for x in report['results']) and report['gl_error']==0
    finally:
        gl.close(); (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
