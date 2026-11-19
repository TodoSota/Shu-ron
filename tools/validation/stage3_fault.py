"""Production P2G/G2P fault reporting and step rejection, with actual GL buffers."""
import argparse
import json
from pathlib import Path
import numpy as np
from run import GL
from stage2 import Pipeline


def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--ssbo',action='store_true')
    args=parser.parse_args()
    dest=args.output; dest.mkdir(parents=True,exist_ok=False)
    fixtures=[('identity',np.eye(3),False),('near',np.diag([1,1,1e-5]),True),
              ('zero',np.zeros((3,3)),True),('inverted',np.diag([1,1,-1]),True),
              ('nonfinite',np.diag([1,1,np.nan]),True),('repeated',.8*np.eye(3),False)]
    (dest/'plan.json').write_text(json.dumps(dict(inputs=[x[0] for x in fixtures],
        expected='reject anomalous P2G input and G2P trial, preserve input buffer; normal inputs run',
        orthogonality_limit=5e-4,dt=.00025),indent=2),encoding='utf-8')
    report=dict(results=[]); gl=GL(dest); report['gpu']=gl.info
    physics=np.zeros(28,np.float32); physics[3]=.00025; physics[5]=1; physics[7]=-10
    physics[10:12]=1.5/128,128/1.5
    physics[13:17]=2.7e-6,50000/2.6,50000*.3/(1.3*.4),.00108
    try:
        for name,f,reject in fixtures:
            folder=dest/name; folder.mkdir(); gl.output=folder
            pipe=Pipeline(gl,physics,ssbo=args.ssbo)
            particles=np.zeros((1,48),np.float32); particles[0,:3]=.75
            f4=np.eye(4); f4[:3,:3]=f; particles[0,24:40]=f4.T.ravel();particles[0,40]=.267765
            pipe.upload(particles); error=None
            try: pipe.step()
            except RuntimeError as exc: error=str(exc)
            fault=gl.read(pipe.fault,(24,)).view(np.uint32)
            unchanged=pipe.read().tobytes()==particles.tobytes()
            report['results'].append(dict(name=name,stage=int(fault[3]),reason=int(fault[2]),
                error=error,unchanged=unchanged,passed=(bool(error)==reject and (not reject or (fault[3]==1 and unchanged)))))
            pipe.close()
        for name,gradient,reject in [('trial-inverted',-8000.,True),('trial-near',-3999.96,True),('trial-normal',.1,False)]:
            folder=dest/name;folder.mkdir();gl.output=folder
            pipe=Pipeline(gl,physics,ssbo=args.ssbo)
            particles=np.zeros((1,48),np.float32);particles[0,:3]=.75
            particles[0,24:40]=np.eye(4).ravel();particles[0,40]=.267765
            pipe.upload(particles)
            fields=np.zeros((4,128,128,128),np.float32)
            fields[3]=1
            fields[0]=(np.arange(128)*1.5/128-.75)*gradient
            pipe.upload_grid(fields)
            gl.dispatch(pipe.programs[3],1)
            fault=gl.read(pipe.fault,(24,)).view(np.uint32)
            unchanged=pipe.read().tobytes()==particles.tobytes()
            trial=gl.read(pipe.fault,(24,))[4:20].reshape(4,4).T
            report['results'].append(dict(name=name,stage=int(fault[3]),reason=int(fault[2]),
                trial=trial.tolist(),unchanged=unchanged,
                passed=(bool(fault[0])==reject and unchanged and (not reject or fault[3]==2))))
            pipe.close()
        for row in report['results']: row['passed']=bool(row['passed'])
        report['gl_error']=int(gl.GetError());report['passed']=report['gl_error']==0 and all(x['passed'] for x in report['results'])
    finally:
        gl.close();(dest/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
