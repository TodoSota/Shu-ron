"""GPU linear APIC transfer and stress checks against independent nodal references."""
import argparse
import ctypes as c
import json
from pathlib import Path

import numpy as np

from run import GL
from stage2 import Pipeline
from stage5_linear_reference import AFFINE, OFFSETS

# A binary cell width keeps the knot and adjacent float32 positions exact.
DX = 1/128
MIN_MOMENT = 1/16
LIMITS = dict(mass=3e-6, momentum=3e-5, impulse=5e-4, velocity=3e-5, affine=3e-5)
DT = .00025


def exercise(gl, pipe, physics, location, affine):
    index = np.floor(location).astype(int)
    t = location-index
    nodes = index+OFFSETS
    in_domain = ((nodes>=0)&(nodes<128)).all(axis=1)
    indices = tuple(nodes[in_domain].astype(int).T[::-1])
    offsets = (OFFSETS[in_domain]-t)*DX
    weights = np.where(OFFSETS[in_domain]==0,1-t,t).prod(axis=1)
    moment = t*(1-t)
    velocity = np.array([.2,-.1,.3])
    deformation = np.diag(np.array([.99,.995,.997],np.float32)).astype(float)

    particle = np.zeros((1,48),np.float32)
    particle[0,:4] = [*(location*DX),1]
    particle[0,4:7] = velocity
    particle[0,8:24] = np.pad(affine,((0,1),(0,1))).T.ravel()
    particle[0,24:40] = np.pad(deformation,((0,1),(0,1))).T.ravel()
    particle[0,39] = 1
    particle[0,40] = .267765
    pipe.upload(particle)
    try:
        gl.UseProgram(pipe.programs[0]); gl.DispatchCompute(16,16,16)
        gl.MemoryBarrier(0x2000 if pipe.ssbo else 0x20)
        gl.dispatch(pipe.programs[1],1)
        grid = pipe.read_grid()
        actual_mass = grid[3][indices].astype(float)
        actual_momentum = grid[:3][:,indices[0],indices[1],indices[2]].T.astype(float)
        mass = float(physics[16])
        velocity_field = velocity+offsets@affine.T
        reference_momentum = mass*weights[:,None]*velocity_field
        strain = np.log(np.diag(deformation))
        stress = np.diag(2*float(physics[14])*strain+float(physics[15])*sum(strain))
        impulse = -DT*float(physics[13])*((weights[:,None]*offsets/DX**2)/
                                         np.maximum(moment,MIN_MOMENT))@stress.T
        measured_impulse = actual_momentum-reference_momentum
        mass_expected = mass*weights.sum()
        momentum_expected = (reference_momentum+impulse).sum(axis=0)
        velocity_expected = weights@velocity_field
        affine_expected = (velocity_field.T*weights)@offsets/DX**2/np.maximum(moment,MIN_MOMENT)
        kick = np.divide(np.linalg.norm(measured_impulse,axis=1),actual_mass,
                         out=np.zeros_like(actual_mass),where=actual_mass>0)

        fields = np.zeros_like(grid)
        fields[:3][:,indices[0],indices[1],indices[2]] = (weights[:,None]*velocity_field).T
        fields[3][indices] = weights
        pipe.upload_grid(fields)
        gl.dispatch(pipe.programs[3],1)
        gl.MemoryBarrier(0x200)
        actual = gl.read(pipe.buffers[1],particle.shape)[0]
        result = dict(mass=float(max(abs(grid[3].sum(dtype=np.float64)-mass_expected),
                                     np.linalg.norm(actual_mass-mass*weights))/mass),
                      momentum=float(np.linalg.norm(actual_momentum.sum(axis=0)-momentum_expected)/
                                     np.linalg.norm(mass*velocity)),
                      impulse=float(np.linalg.norm(measured_impulse-impulse)/np.linalg.norm(impulse))
                                   if np.linalg.norm(impulse)>0 else float(np.linalg.norm(measured_impulse)),
                      velocity=float(abs(actual[4:7]-velocity_expected).max()),
                      affine=float(abs(actual[8:24].reshape(4,4).T[:3,:3]-affine_expected).max()),
                      max_nodal_stress_kick=float(kick.max()),
                      support_complete=bool(in_domain.all()))
        if gl.GetError():
            raise RuntimeError('OpenGL error in linear fixture')
        result['passed'] = bool(np.isfinite(actual[:43]).all() and
                                all(result[key]<=limit for key,limit in LIMITS.items()))
        return result
    finally:
        for buffer in pipe.buffers:
            gl.DeleteBuffers(1,c.byref(buffer))
        pipe.buffers.clear()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--ssbo',action='store_true')
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    near=float(np.nextafter(np.float32(64),np.float32(65)))-64
    rotation=np.array([[0.,-2.,0.],[2.,0.,0.],[0.,0.,0.]])
    cases=[
        ('constant',[.25,.5,.75],np.zeros((3,3))),
        ('affine',[.25,.5,.75],AFFINE),
        ('rotation',[.25,.5,.75],rotation),
        ('near-low',[near,.25,.75],AFFINE),
        ('near-high',[1-near,.25,.75],AFFINE),
        ('face',[0.,.5,.75],AFFINE),
        ('edge',[0.,0.,.75],AFFINE),
        ('vertex',[0.,0.,0.],AFFINE),
        ('domain-low',[-64.25,.5,.75],AFFINE),
        ('domain-high',[63.25,.5,.75],AFFINE),
    ]
    (args.output/'plan.json').write_text(json.dumps(dict(limits=LIMITS, floor=MIN_MOMENT,
        dx=DX, cases=[(name, t) for name,t,_ in cases], ssbo=args.ssbo),indent=2))
    gl=GL(args.output)
    physics=np.zeros(28,np.float32)
    physics[3]=DT
    physics[5]=1
    physics[7]=-10
    physics[10:12]=DX,1/DX
    physics[13:17]=2.7e-6,50000/2.6,50000*.3/(1.3*.4),.00108
    pipe=Pipeline(gl,physics,ssbo=args.ssbo,linear=True)
    try:
        rows={}
        for label,fraction,affine in cases:
            location=np.float32(64)+np.array(fraction,np.float32)
            rows[label]=exercise(gl,pipe,physics,location.astype(float),affine)
            print(label, rows[label])
        passed=all(row['passed'] for row in rows.values())
        (args.output/'report.json').write_text(json.dumps(dict(passed=passed,gpu=gl.info,results=rows),indent=2))
        return 0 if passed else 1
    finally:
        pipe.close()
        gl.close()


if __name__=='__main__':
    raise SystemExit(main())

