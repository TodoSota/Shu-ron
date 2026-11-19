"""Continuous wall approach diagnostics, including a deliberately unsafe displacement."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, SHADERS, ROOT
from stage2 import Pipeline, metrics


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true')
    args=p.parse_args()
    suffix='ssbo' if args.ssbo else '3dtex'
    shader_dir=SHADERS.parent/'ssbo' if args.ssbo else SHADERS
    args.output.mkdir(parents=True,exist_ok=False)
    plan=dict(dt=.001,steps=100,speeds=[1,10,100],grid=128,
              initial='one particle, 4.5 dx from low end / 5.5 dx from high end',
              gravity=False,floor=False,sdf=False,
              stop='nonfinite, stored or trial detF<=0, support outside grid',
              unsafe_speed=100,note='unsafe case diagnoses the absence of a universal containment guarantee')
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    report=dict(cases=[],hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                                for f in [Path(__file__),ROOT/'tools/validation/stage2.py',
                                          *[shader_dir/f'mpm_{s}_{suffix}.comp' for s in ['setup','p2g','grid','g2p']]]})
    gl=GL(args.output); report['gpu']=gl.info
    try:
        physics=np.zeros(28,np.float32); physics[3]=.001
        physics[5]=1; physics[7]=-10; physics[9:12]=.6,1.5/128,128/1.5
        physics[13:18]=2.7e-6,50000/2.6,50000*.3/(1.3*.4),.00108,1.5/256
        # Reuse the same grid allocation; every step starts with production Setup.
        pipe=Pipeline(gl,physics,ssbo=args.ssbo)
        for axes in [[0],[1],[2],[0,1,2]]:
            for side in [0,1]:
                for speed in plan['speeds']:
                    label=f'axes-{axes}-side-{side}-speed-{speed}'
                    a=np.zeros((1,48),np.float32); a[0,:3]=.75; a[0,3]=1
                    a[0,24:40]=np.eye(4,dtype=np.float32).ravel(); a[0,40]=.267765
                    a.view(np.int32)[0,43]=1
                    for axis in axes:
                        a[0,axis]=(122.5 if side else 4.5)*physics[10]
                        a[0,4+axis]=speed*(1 if side else -1)
                    pipe.upload(a)
                    case=dict(name=label,unsafe=speed==100,initial=a[0,:7].tolist())
                    report['cases'].append(case)
                    for step in range(1,101):
                        pipe.step(); b=pipe.read(); m=metrics(b)
                        affine=b[0,8:24].reshape(4,4).T[:3,:3].astype(float)
                        f=a[0,24:40].reshape(4,4).T[:3,:3].astype(float)
                        trial_det=float(np.linalg.det((np.eye(3)+.001*affine)@f))
                        if not m['finite'] or m['det_min']<=0 or trial_det<=0 or m['support_outside']:
                            case['failure']=dict(step=step,time=step*.001,trial_det=trial_det,**m)
                            a.tofile(args.output/(label+'-previous.bin')); b.tofile(args.output/(label+'-failure.bin'))
                            break
                        a=b
                    case['completed_steps']=step; case['final']=metrics(b)
                    case['passed']='failure' not in case
                    print(label,case['passed'],case.get('failure',{}).get('step'),flush=True)
                    for buf in pipe.buffers: gl.DeleteBuffers(1,c.byref(buf))
                    pipe.buffers=[]
        pipe.close()
        report['normal_passed']=all(x['passed'] for x in report['cases'] if not x['unsafe'])
    finally:
        gl.close(); (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['normal_passed'] else 1


if __name__=='__main__': raise SystemExit(main())
