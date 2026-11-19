"""Independent trilinear transfer reference for the stage 5 formulation decision."""
import argparse
import itertools
import json
from pathlib import Path

import numpy as np

DX = 1.5 / 128
OFFSETS = np.array(list(itertools.product((0., 1.), repeat=3)))
AFFINE = np.array([[.3, -.2, .1], [.2, -.4, .05], [.05, -.1, .2]])
STRESS = np.array([[2., -.3, .4], [-.3, -1., .6], [.4, .6, 1.]])
LIMIT = 1e-8


def fixture(fraction):
    t = np.array(fraction)
    factors = np.where(OFFSETS == 0., 1-t, t)
    weights = factors.prod(axis=1)
    offsets = (OFFSETS-t)*DX
    moment = (offsets.T * weights) @ offsets
    diagonal = DX**2*t*(1-t)
    gradients = np.stack([
        (2*OFFSETS[:,axis]-1)*factors[:,[j for j in range(3) if j!=axis]].prod(axis=1)/DX
        for axis in range(3)
    ],axis=1)
    velocities = np.array([.2,-.1,.3])+offsets @ AFFINE.T
    sampled = (velocities.T*weights) @ offsets
    inverse = np.divide(1.,diagonal,out=np.zeros_like(diagonal),where=diagonal!=0.)
    active = diagonal!=0.
    expected = AFFINE*active
    covered = (weights > 0)
    # A knot node with zero weight need not have a grid velocity supplied by P2G.
    dp_affine = sampled*inverse
    gradient_affine = velocities.T @ gradients
    masked_gradient_affine = velocities[covered].T @ gradients[covered]
    dp_force = -((weights[:,None]*offsets)*inverse) @ STRESS.T
    gradient_force = -gradients @ STRESS.T
    result = dict(fraction=fraction,active_axes=active.tolist(),
                  dp_error=float(abs(moment-np.diag(diagonal)).max()),
                  dp_affine_error=float(abs(dp_affine-expected).max()),
                  gradient_affine_error=float(abs(gradient_affine-AFFINE).max()),
                  gradient_with_empty_nodes_error=float(abs(masked_gradient_affine-AFFINE).max()),
                  dp_vs_gradient_force=float(abs(dp_force-gradient_force).max()),
                  force_sum=float(abs(gradient_force.sum(axis=0)).max()),
                  old_c_error=float(abs(sampled*(4/DX)-AFFINE).max()),
                  constant_quadratic_c_error=float(abs(sampled*(4/DX**2)-AFFINE).max()))
    result['passed'] = (result['dp_error']<LIMIT and result['dp_affine_error']<LIMIT
                        and result['gradient_affine_error']<LIMIT and result['force_sum']<LIMIT
                        and (not active.all() or result['dp_vs_gradient_force']<LIMIT))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    output = parser.parse_args().output
    output.mkdir(parents=True,exist_ok=False)
    samples = [(0.5,0.5,0.5),(0.25,0.5,0.75),(1e-7,0.25,1-1e-7),
               (0.,0.5,0.75),(0.,0.,0.75),(0.,0.,0.)]
    (output/'plan.json').write_text(json.dumps(dict(dx=DX,absolute_tolerance=LIMIT,samples=samples),indent=2))
    rows = [fixture(t) for t in samples]
    report = dict(passed=all(row['passed'] for row in rows),results=rows)
    (output/'report.json').write_text(json.dumps(report,indent=2))
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())

