"""Combine completed dt runs and check final-state P2G mass on the GPU."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from run import GL, ROOT, SHADERS, read_shader
from stage2 import Pipeline


def executable_text(text):
    return re.sub(r'\s+','',re.sub(r'//[^\n]*|/\*.*?\*/','',text,flags=re.S))


def main():
    p=argparse.ArgumentParser(); p.add_argument('--output',type=Path,required=True)
    p.add_argument('--runs',type=Path,nargs='+',required=True)
    p.add_argument('--regression',type=Path,required=True)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    plan=dict(position_rms=.02,centroid=.01,aabb=.03,mass_relative=3e-6,
              provenance='Every compiled shader must match current executable text (comments/whitespace excluded)')
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    report=dict(runs=[str(x) for x in args.runs],mass=[],comparisons=[],
                hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                        for f in [Path(__file__),ROOT/'tools/validation/stage2.py',
                                  *[SHADERS/('mpm_'+s+'_3dtex.comp') for s in ['setup','p2g','grid','g2p']]]})
    known=[executable_text(read_shader(SHADERS/('mpm_'+s+'_3dtex.comp')))
           for s in ['setup','p2g','grid','g2p']]
    states={}
    for run in args.runs:
        data=json.loads((run/'report.json').read_text(encoding='utf-8'))
        for shader in run.glob('shader-*.comp'):
            if executable_text(read_shader(shader)) not in known:
                raise RuntimeError('Different executable shader: '+str(shader))
        for case in data['cases']:
            if case['name'].startswith('fall-') and case['passed']:
                states[case['dt']]=np.fromfile(run/(case['name']+'-final.bin'),np.float32).reshape(-1,48)
    gl=GL(args.output); report['gpu']=gl.info
    try:
        physics=np.fromfile(args.regression/'initialization/physics.bin',np.float32).reshape(2,28)[0]
        pipe=Pipeline(gl,physics)
        for dt,a in sorted(states.items(),reverse=True):
            pipe.upload(a)
            gl.UseProgram(pipe.programs[0]); gl.DispatchCompute(16,16,16); gl.MemoryBarrier(0x20)
            gl.UseProgram(pipe.programs[1]); gl.DispatchCompute((len(a)+63)//64,1,1); gl.MemoryBarrier(0x100); gl.Finish()
            field=np.empty((128,128,128),np.float32); gl.BindTexture(0x806F,pipe.textures[3])
            gl.GetTexImage(0x806F,0,0x1903,0x1406,field.ctypes.data)
            mass=float(field.astype(float).sum()); expected=len(a)*float(physics[16])
            err=abs(mass-expected)/expected
            report['mass'].append(dict(dt=dt,mass=mass,relative_error=err,passed=err<=plan['mass_relative']))
            for buf in pipe.buffers: gl.DeleteBuffers(1,c.byref(buf))
            pipe.buffers=[]
        pipe.close(); report['gl_error']=int(gl.GetError())
    finally:
        gl.close()
    dts=sorted(states,reverse=True)
    for coarse,fine in zip(dts[:-1],dts[1:]):
        a,b=states[coarse][:,:3].astype(float),states[fine][:,:3].astype(float)
        comparison=dict(coarse=coarse,fine=fine,
                        position_rms=float(np.sqrt(np.mean(np.sum((a-b)**2,axis=1)))),
                        centroid=float(np.linalg.norm(a.mean(0)-b.mean(0))),
                        aabb=float(np.max(np.abs(np.array([a.min(0),a.max(0)])-np.array([b.min(0),b.max(0)])))))
        comparison['passed']=all(comparison[k]<=plan[k] for k in ['position_rms','centroid','aabb'])
        report['comparisons'].append(comparison)
    (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report['comparisons'],indent=2))
    print(json.dumps(report['mass'],indent=2))


if __name__=='__main__': main()
