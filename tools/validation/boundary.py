"""Direct production Grid fixtures, including symmetric three-node boundary bands."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, ROOT, SHADERS
from stage2 import Pipeline


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true')
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    fixtures=[]
    for axis in range(3):
        for side in [0,1]:
            for inward in [False,True]:
                xyz=[64,64,64]; xyz[axis]=2 if side==0 else 125
                v=np.zeros(3); v[axis]=(1 if side else -1)*(-1 if inward else 1)
                fixtures.append((f'face-{axis}-{side}-{inward}',xyz,v,v if inward else np.zeros(3)))
    fixtures += [('corner-low',[2,2,2],[-1,-1,-1],[0,0,0]),
                 ('corner-high',[125,125,125],[1,1,1],[0,0,0]),
                 ('floor-slip',[40,8,40],[2,-1,0],[1.4,0,0]),
                 ('floor-stick',[42,8,42],[.2,-1,0],[0,0,0]),
                 ('floor-separate',[44,8,44],[2,1,0],[2,1,0]),
                 ('above-floor',[46,9,46],[2,-1,0],[2,-1,0])]
    plan=dict(fixtures=[dict(name=n,xyz=x,velocity=list(v),expected=list(e)) for n,x,v,e in fixtures],
              tolerance=2e-6,gravity=[0,0,0],floor=.1,friction=.6,
              sdf='excluded with AABB [10,11]^3',boundary='three nodes at each end')
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    grid_shader=(SHADERS.parent/'ssbo/mpm_grid_ssbo.comp' if args.ssbo else SHADERS/'mpm_grid_3dtex.comp')
    report=dict(results=[],hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                                 for f in [Path(__file__),ROOT/'tools/validation/stage2.py',grid_shader]})
    gl=None; pipe=None
    try:
        gl=GL(args.output); report['gpu']=gl.info
        physics=np.zeros(28,np.float32)
        physics[5:8]=1,0,.1; physics[9:12]=.6,1.5/128,128/1.5
        pipe=Pipeline(gl,physics,ssbo=args.ssbo)
        # Separate dispatches avoid overlap between inward and outward face fixtures.
        for name,xyz,v,expected in fixtures:
            fields=np.zeros((4,128,128,128),np.float32)
            x,y,z=xyz; fields[:3,z,y,x]=v; fields[3,z,y,x]=1
            pipe.upload_grid(fields)
            gl.UseProgram(pipe.programs[2])
            gl.DispatchCompute(16,16,16)
            actual=pipe.read_grid()[:3,z,y,x].tolist()
            error=float(np.max(np.abs(np.array(actual)-expected)))
            report['results'].append(dict(name=name,actual=actual,expected=list(expected),error=error,passed=error<=plan['tolerance']))
            print(name,actual,'PASS' if error<=plan['tolerance'] else 'FAIL',flush=True)
        report['gl_error']=int(gl.GetError())
        report['passed']=all(r['passed'] for r in report['results']) and report['gl_error']==0
    finally:
        if pipe: pipe.close()
        if gl: gl.close()
        (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
