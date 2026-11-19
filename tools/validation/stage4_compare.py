"""Compare production TEX and SSBO using identical particle input."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path

import numpy as np

from run import GL, ROOT, SHADERS
from stage2 import Pipeline, shader_filename

DX = 1.5 / 128
DT = .00025
LIMITS = dict(first_position_m=DX*1e-4, first_velocity_mps=1e-5,
              first_state_max=5e-4, mass_relative=3e-6,
              final_position_rms_m=DX/2, final_centroid_m=DX/10,
              final_aabb_m=DX, final_rms_speed_delta_mps=.02)


def first_step(gl, initial, physics, linear):
    result = []
    for ssbo in (False, True):
        pipe = Pipeline(gl, physics, ssbo=ssbo, linear=linear)
        try:
            # Poison every node. Production Setup must clear the entire grid.
            pipe.upload_grid(np.ones((4,128,128,128),np.float32))
            gl.UseProgram(pipe.programs[0])
            gl.DispatchCompute(16,16,16)
            cleared = bool(np.count_nonzero(pipe.read_grid()) == 0)
            pipe.upload(initial)
            pipe.step()
            particles = pipe.read()
            mass = float(pipe.read_grid()[3].sum(dtype=np.float64))
            expected = float(physics[16])*len(initial)
            result.append((particles, cleared, abs(mass-expected)/expected))
        finally:
            pipe.close()
    tex, ssbo = result
    delta = tex[0].astype(float)-ssbo[0]
    return dict(first_position_m=float(np.linalg.norm(delta[:,:3],axis=1).max()),
                first_velocity_mps=float(np.linalg.norm(delta[:,4:7],axis=1).max()),
                first_state_max=float(abs(delta[:,8:43]).max()),
                first_discrete_state_equal=bool(np.array_equal(tex[0][:,43:45].view(np.int32),
                                                               ssbo[0][:,43:45].view(np.int32))),
                mass_relative=max(tex[2],ssbo[2]), setup_cleared=tex[1] and ssbo[1])


def compare_final(tex, ssbo):
    a, b = tex[:,:3].astype(float), ssbo[:,:3].astype(float)
    return dict(final_position_rms_m=float(np.sqrt(np.mean(np.sum((a-b)**2,axis=1)))),
                final_centroid_m=float(np.linalg.norm(a.mean(0)-b.mean(0))),
                final_aabb_m=float(np.max(abs(np.array([a.min(0)-b.min(0),a.max(0)-b.max(0)])))),
                final_rms_speed_delta_mps=float(abs(np.sqrt(np.mean(np.sum(tex[:,4:7]**2,axis=1)))-
                                                    np.sqrt(np.mean(np.sum(ssbo[:,4:7]**2,axis=1))))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--regression', type=Path, required=True)
    parser.add_argument('--continuous-root', type=Path)
    parser.add_argument('--first-only', action='store_true')
    parser.add_argument('--linear', action='store_true')
    args = parser.parse_args()
    root = args.continuous_root
    if not args.first_only and root is None:
        parser.error("--continuous-root is required for the final comparisons")
    prefix = "stage5-dp" if args.linear else "stage4"
    output = args.output
    output.mkdir(parents=True, exist_ok=False)
    (output/'plan.json').write_text(json.dumps(dict(limits=LIMITS, dx=DX, dt=DT, linear=args.linear,
        initial_particles=10000, rendering=False, evaluation=False, history=False,
        rationale='Single-step roundoff and local state tolerance. After contact, compare at sub-cell position scales and with a speed delta below the settling criterion.'),indent=2))
    sources = [Path(__file__), ROOT/'tools/validation/stage2.py']
    for suffix in ('3dtex','ssbo'):
        folder = SHADERS if suffix=='3dtex' else SHADERS.parent/'ssbo'
        sources.extend(folder/shader_filename(stage,suffix,args.linear) for stage in ('setup','p2g','grid','g2p'))
    report = dict(first={}, final={}, inputs={})
    def record(path):
        report['inputs'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        return path
    for source in sources:
        record(source)
    initial = args.regression/'initialization'
    gl = GL(output)
    try:
        report['gpu'] = gl.info
        get_limit = gl.glfw.glfwGetProcAddress(b'glGetIntegerv')
        query = c.WINFUNCTYPE(None,c.c_uint,c.POINTER(c.c_int))(get_limit)
        for name, token in [('ssbo_block_bytes',0x90DE),('ssbo_bindings',0x90DD)]:
            value = c.c_int()
            query(token,c.byref(value))
            report[name] = value.value
        for shape, file in [('cube','physics.bin'),('sphere','shape-physics.bin')]:
            particles = np.fromfile(record(initial/f'{shape}.bin'), np.float32).reshape(-1,48)
            physics = np.fromfile(record(initial/file), np.float32).reshape(-1,28)[0]
            physics[3] = DT
            report['first'][shape] = first_step(gl,particles,physics,args.linear)
    finally:
        gl.close()
    if not args.first_only:
        peer = root/('stage5-dp-reg-1' if args.linear else 'stage4-final-1-regression')/'initialization'
        for name in ('cube.bin','sphere.bin','physics.bin','shape-physics.bin'):
            if record(peer/name).read_bytes() != record(initial/name).read_bytes():
                raise RuntimeError(f'TEX/SSBO inputs differ: {name}')
        for case in ('cube','sphere','static-isotropic','translate-isotropic','rotate-isotropic','rotate-Swing'):
            pair = []
            for index in (0,1):
                fixture = root/f'{prefix}-{index}-{case if case in ("cube","sphere") else "obstacles"}'
                run = json.loads(record(fixture/'report.json').read_text())
                if not run['passed']:
                    raise RuntimeError(f'Independent validation failed: {fixture}')
                path = fixture/'fall-0.00025-final.bin' if case in ('cube','sphere') else fixture/case/'final.bin'
                pair.append(np.fromfile(record(path),np.float32).reshape(-1,48))
            report['final'][case] = compare_final(*pair)
    report['passed'] = all(
        all(value<=LIMITS[key] if key in LIMITS else value for key,value in row.items())
        for row in [*report['first'].values(),*report['final'].values()])
    (output/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps({key:report[key] for key in ('first','final','passed')},indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())

