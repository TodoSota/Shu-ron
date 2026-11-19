"""Diagnose production SVD over unsupported inputs; no material policy is adopted."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, SHADERS, U, section, relative, read_shader


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    dest = p.parse_args().output
    dest.mkdir(parents=True, exist_ok=False)
    a = .61
    r = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
    cases = dict(positive=np.diag([.7, 1.1, 1.4]), repeated=np.eye(3)*.8,
                 near_singular=np.diag([1, 1, 1e-5]), zero=np.zeros((3, 3)),
                 inverted=np.diag([1, 1, -1]), rotated_near=r @ np.diag([1, 1e-5, .8]) @ r.T,
                 rotated_inverted=r @ np.diag([1, -.8, 1.1]))
    # Record acceptance criteria before executing. Unsupported cases are diagnostics.
    plan = dict(tolerance=5e-4, cases={k: v.tolist() for k, v in cases.items()},
                supported=['positive', 'repeated'], purpose='SVD only; not a constitutive-model acceptance test')
    (dest/'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    report = dict(results=[], hashes={}, material=[], policy_comparison=[])
    gl = GL(dest)
    report['gpu'] = gl.info
    try:
        for filename in ['mpm_p2g_3dtex.comp', 'mpm_g2p_3dtex.comp']:
            path = SHADERS/filename
            source = read_shader(path)
            report['hashes'][filename] = hashlib.sha256(path.read_bytes()).hexdigest()
            shader = '''#version 430
layout(local_size_x=64) in;
layout(std430,binding=4) readonly buffer Input { mat4 fs[]; };
layout(std430,binding=5) writeonly buffer Output { mat4 data[]; };
''' + section(source, 'void jacobiEigen(', 'void main()') + '''
void main(){uint i=gl_GlobalInvocationID.x; if(i>=fs.length())return;
mat3 u,s,v; svd(mat3(fs[i]),u,s,v);
data[3*i]=mat4(u); data[3*i+1]=mat4(s); data[3*i+2]=mat4(v);}
'''
            fs = np.tile(np.eye(4, dtype=np.float32), (len(cases), 1, 1))
            fs[:, :3, :3] = list(cases.values())
            inp = gl.buffer(fs.transpose(0, 2, 1).copy(), 4)
            out = gl.buffer(np.zeros((len(cases)*3, 4, 4), np.float32), 5)
            program = gl.program(shader)
            gl.dispatch(program, len(cases))
            raw = gl.read(out, (len(cases), 3, 4, 4)).transpose(0, 1, 3, 2)
            np.save(dest/(filename+'.npy'), raw)
            for (name, f), triple in zip(cases.items(), raw):
                u, s, v = triple[:, :3, :3].astype(float)
                metrics = dict(reconstruction=relative(u @ s @ v.T, f),
                               orthogonality=max(relative(u.T @ u, np.eye(3)), relative(v.T @ v, np.eye(3))),
                               singular_values=relative(np.sort(np.diag(s)), np.sort(np.linalg.svd(f, compute_uv=False))))
                row = dict(shader=filename, name=name, finite=bool(np.isfinite(triple).all()),
                           det_f=float(np.linalg.det(f)), det_u=float(np.linalg.det(u)), metrics=metrics)
                if name in plan['supported']:
                    row['passed'] = row['finite'] and max(metrics.values()) <= plan['tolerance']
                report['results'].append(row)
            gl.DeleteProgram(program)
            for buf in [inp, out]:
                gl.DeleteBuffers(1, c.byref(buf))
        # Compare all policies on the same trial F and a valid previous F=I.
        # Regularization is an independent CPU reference candidate, not adopted GLSL.
        inputs = []
        labels = []
        for name, f in cases.items():
            u, s, vt = np.linalg.svd(f)
            orientation = np.ones(3)
            if np.linalg.det(u @ vt) < 0:
                orientation[np.argmin(s)] = -1
            regularized = u @ np.diag(np.maximum(s, .01)*orientation) @ vt
            report['policy_comparison'].append(dict(name=name, previous_f=np.eye(3).tolist(),
                trial_det=float(np.linalg.det(f)), hold_det=1.,
                regularized_det=float(np.linalg.det(regularized)),
                regularization_change=float(np.linalg.norm(regularized-f)),
                regularized_f=regularized.tolist(),
                stop='no next physical step', hold='previous full particle state; discards this trial update',
                regularize='CPU nearest positive determinant candidate with singular-value floor .01; changes material state'))
            for policy, matrix in [('current', f), ('hold', np.eye(3)), ('regularize', regularized)]:
                row = np.zeros(48, np.float32)
                f4 = np.eye(4); f4[:3, :3] = matrix
                row[24:40] = f4.T.ravel(); row[40] = .267765
                inputs.append(row); labels.append((name, policy))
        source = read_shader((SHADERS/'mpm_g2p_3dtex.comp'))
        shader = section(source, '#version 430 core', 'void main()')
        shader += '''void main(){uint idx=gl_GlobalInvocationID.x;
if(idx>=particleIn.length())return; particleOut[idx]=particleIn[idx];
mat3 new_C=mat3(0); vec3 new_v=vec3(0);
''' + source[source.index('\tmat3 F_new ='):]
        particles = np.array(inputs, np.float32)
        physics = np.zeros(28, np.float32)
        physics[14:16] = 50000/2.6, 50000*.3/(1.3*.4)
        ubo = gl.buffer(physics, 1, 0x8A11)
        inp = gl.buffer(particles, 0)
        out = gl.buffer(np.zeros_like(particles), 1)
        program = gl.program(shader)
        gl.dispatch(program, len(particles))
        actual = gl.read(out, particles.shape)
        np.save(dest/'material-input.npy', particles)
        np.save(dest/'material-output.npy', actual)
        for (name, policy), row in zip(labels, actual):
            f = row[24:40].reshape(4, 4).T[:3, :3]
            report['material'].append(dict(name=name, policy=policy,
                finite=bool(np.isfinite(row[:43]).all()), projected_det=float(np.linalg.det(f)),
                alpha_q_vc=[float(x) if np.isfinite(x) else None for x in row[40:43]],
                state=int(row.view(np.int32)[43])))
        gl.DeleteProgram(program)
        for buf in [ubo, inp, out]:
            gl.DeleteBuffers(1, c.byref(buf))
        report['gl_error'] = int(gl.GetError())
        report['passed'] = report['gl_error'] == 0 and all(x.get('passed', True) for x in report['results'])
    finally:
        gl.close()
        (dest/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
