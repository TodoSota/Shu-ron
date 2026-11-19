"""Paired diagnostic: production mass cutoff versus positive-mass nodes only."""
import argparse
import hashlib
import json
import numpy as np
from run import GL, SHADERS, ROOT, read_shader
from stage2 import Pipeline, metrics


def main():
    from pathlib import Path
    p=argparse.ArgumentParser()
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--regression',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true')
    args=p.parse_args()
    suffix='ssbo' if args.ssbo else '3dtex'
    shader_dir=SHADERS.parent/'ssbo' if args.ssbo else SHADERS
    args.output.mkdir(parents=True,exist_ok=False)
    plan=dict(steps=50,dt=.001,velocity=[.2,-.1,.3],gravity=[0,0,0],
              velocity_tolerance=.002,position_tolerance=.0002,
              variants=['legacy-cutoff','positive-mass-only'],note='mutations in test compilation only')
    (args.output/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    cube=np.fromfile(args.regression/'initialization/cube.bin',np.float32).reshape(-1,48)
    physics=np.fromfile(args.regression/'initialization/physics.bin',np.float32).reshape(2,28)[0]
    physics[:3]=0
    physics[3]=plan['dt']
    report=dict(cases=[],hashes={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
                                for f in [Path(__file__),ROOT/'tools/validation/stage2.py',
                                          shader_dir/f'mpm_grid_{suffix}.comp',shader_dir/f'mpm_g2p_{suffix}.comp']})
    gl=GL(args.output); report['gpu']=gl.info
    try:
        for variant in plan['variants']:
            pipe=Pipeline(gl,physics,ssbo=args.ssbo)
            for i,name,legacy in [(2,'grid','m <= 1e-7'),(3,'g2p','m < 1e-7')]:
                source=read_shader((shader_dir/f'mpm_{name}_{suffix}.comp'))
                old=legacy if legacy in source else 'm <= 0.0'
                if source.count(old)!=1: raise RuntimeError('Cutoff anchor changed')
                gl.DeleteProgram(pipe.programs[i])
                pipe.programs[i]=gl.program(source.replace(old,legacy if variant=='legacy-cutoff' else 'm <= 0.0'))
                if args.ssbo:
                    gl.UseProgram(pipe.programs[i])
                    gl.set_grid_size(pipe.programs[i],128)
            initial=cube.copy(); initial[:,4:7]=plan['velocity']; pipe.upload(initial)
            for _ in range(plan['steps']): pipe.step()
            a=pipe.read(); a.tofile(args.output/(variant+'.bin'))
            velocity_error=float(np.linalg.norm(a[:,4:7]-np.array(plan['velocity']),axis=1).max())
            position_error=float(np.linalg.norm(a[:,:3]-initial[:,:3]-.05*np.array(plan['velocity']),axis=1).max())
            result=dict(variant=variant,velocity_error=velocity_error,position_error=position_error,**metrics(a))
            report['cases'].append(result); print(json.dumps(result),flush=True)
            pipe.close()
        legacy,positive=report['cases']
        report['legacy_error_detected']=legacy['velocity_error']>plan['velocity_tolerance']
        report['positive_mass_passed']=positive['velocity_error']<=plan['velocity_tolerance'] and positive['position_error']<=plan['position_tolerance']
    finally:
        gl.close(); (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['legacy_error_detected'] and report['positive_mass_passed'] else 1


if __name__=='__main__': raise SystemExit(main())
