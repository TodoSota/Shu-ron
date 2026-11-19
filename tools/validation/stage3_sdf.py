"""Read production SDF mapping/distance/normal expressions on analytic planes."""
import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import numpy as np
from run import GL, SHADERS, U, I, P, section, read_shader


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--distance-mode', choices=['production', 'average', 'gradient'], default='production')
    args = parser.parse_args()
    dest = args.output
    dest.mkdir(parents=True, exist_ok=False)
    path = SHADERS/'mpm_grid_3dtex.comp'
    source = read_shader(path)
    mapping = section(source, 'vec3 sdf_extent =', '// 6.')
    normal = section(source, 'vec3 texSize =', '// 8.')
    # Only the branch and collision response are removed; sampling expressions stay verbatim.
    candidate = {
        'production': '',
        'average': '\ndist_world=dist_local*(length(sdf_model[0].xyz)+length(sdf_model[1].xyz)+length(sdf_model[2].xyz))/3.0;',
        'gradient': '\ndist_world=dist_local / length(mat3(transpose(sdf_inv_model))*normal_local);'
    }[args.distance_mode]
    shader = '''#version 430
layout(local_size_x=64) in;
layout(binding=0) uniform sampler3D sdf_tex;
uniform mat4 sdf_model, sdf_inv_model;
uniform vec3 sdf_aabb_min, sdf_aabb_max;
layout(std430,binding=4) readonly buffer Input { vec4 points[]; };
layout(std430,binding=5) writeonly buffer Output { vec4 data[]; };
layout(std430,binding=7) writeonly buffer LocalOutput { float localdata[]; };
void main(){uint i=gl_GlobalInvocationID.x; if(i>=points.length())return;
vec3 pos_local=points[i].xyz;
''' + mapping + normal + candidate + '\ndata[i]=vec4(normal_world,dist_world); localdata[i]=dist_local;}'
    plan = dict(resolution=[16, 32, 64], extent=[2., 4., 6.],
                sampling='interior normal/distance plus AABB-endpoint distance, analytic plane, GL_LINEAR / CLAMP_TO_EDGE',
                distance_abs=2e-6, normal_abs=2e-5,
                transforms=['isotropic rotated', 'Swing rotated'], distance_mode=args.distance_mode,
                limitation='No CPU mesh generation, boundary gradients, curved surfaces, or collision response')
    (dest/'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    report = dict(results=[], hashes={path.name: hashlib.sha256(path.read_bytes()).hexdigest(),
                                    'tool': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
    gl = GL(dest)
    report['gpu'] = gl.info
    try:
        for name, ret, signature in [('GetUniformLocation', I, [U, c.c_char_p]),
                               ('Uniform3f', None, [I, c.c_float, c.c_float, c.c_float]),
                               ('UniformMatrix4fv', None, [I, I, U, P])]:
            setattr(gl, name, c.WINFUNCTYPE(ret, *signature)(gl.glfw.glfwGetProcAddress(('gl'+name).encode())))
        program = gl.program(shader)
        gl.UseProgram(program)
        extent = np.array(plan['extent'])
        for name, values in [('sdf_aabb_min', -extent/2), ('sdf_aabb_max', extent/2)]:
            gl.Uniform3f(gl.GetUniformLocation(program, name.encode()), *values)
        angle = .61
        rotation = np.array([[np.cos(angle), -np.sin(angle), 0], [np.sin(angle), np.cos(angle), 0], [0, 0, 1]])
        nx, ny, nz = plan['resolution']
        z, y, x = np.meshgrid(np.linspace(-3, 3, nz), np.linspace(-2, 2, ny), np.linspace(-1, 1, nx), indexing='ij')
        coords = np.stack([x, y, z], axis=-1)
        tex = U()
        gl.GenTextures(1, c.byref(tex))
        gl.BindTexture(0x806F, tex)
        for key in [0x2801, 0x2800]:
            gl.TexParameteri(0x806F, key, 0x2601)
        for key in [0x2802, 0x2803, 0x8072]:
            gl.TexParameteri(0x806F, key, 0x812F)
        # Sample texel centers to separate coordinate/gradient correctness from
        # implementation-dependent subtexel interpolation weight precision.
        indices = np.array([[9, 19, 39], [10, 12, 43], [5, 20, 27],
                            [0,19,39],[15,12,43],[5,0,27],[5,31,27],[5,20,0],[5,20,63]])
        points = np.ones((len(indices), 4), np.float32)
        points[:, :3] = -extent/2 + indices*extent/(np.array([nx, ny, nz])-1)
        inp = gl.buffer(points, 4)
        out = gl.buffer(np.zeros_like(points), 5)
        localout = gl.buffer(np.zeros(len(points),np.float32),7)
        for label, scale in [('isotropic', [.2]*3), ('Swing', [.06, .4, .06])]:
            model = np.eye(4)
            model[:3, :3] = rotation @ np.diag(scale)
            for uniform, mat in [('sdf_model', model), ('sdf_inv_model', np.linalg.inv(model))]:
                data = np.ascontiguousarray(mat.T, np.float32)
                gl.UniformMatrix4fv(gl.GetUniformLocation(program, uniform.encode()), 1, 0, data.ctypes.data)
            for name, n in [('x', [1., 0, 0]), ('y', [0, 1., 0]), ('oblique', [1., 2., 3.])]:
                n = np.array(n)/np.linalg.norm(n)
                sdf = np.ascontiguousarray(coords @ n, np.float32)
                gl.TexImage3D(0x806F, 0, 0x822E, nx, ny, nz, 0, 0x1903, 0x1406, sdf.ctypes.data)
                gl.dispatch(program, len(points))
                actual = gl.read(out, points.shape).astype(float)
                sampled_local = gl.read(localout,(len(points),)).astype(float)
                world_gradient = np.linalg.inv(model[:3, :3]).T @ n
                exact_normal = world_gradient/np.linalg.norm(world_gradient)
                exact_distance = (points[:, :3] @ n)/np.linalg.norm(world_gradient)
                normal_error = float(np.max(np.linalg.norm(actual[:3, :3]-exact_normal, axis=1)))
                boundary_normal_error = float(np.max(np.linalg.norm(actual[3:, :3]-exact_normal, axis=1)))
                distance_error = float(np.max(abs(actual[:3, 3]-exact_distance[:3])))
                mapping_error = float(np.max(abs(sampled_local-points[:,:3]@n)))
                row = dict(transform=label, plane=name, actual=actual.tolist(), exact_distance=exact_distance.tolist(),
                           normal_error=normal_error, boundary_normal_error=boundary_normal_error, distance_error=distance_error,
                           mapping_error=mapping_error,mapping_passed=mapping_error<=plan['distance_abs'],
                           distance_relative=float(np.max(abs(actual[:, 3]/exact_distance-1))),
                           normal_passed=normal_error <= plan['normal_abs'])
                if label == 'isotropic' or args.distance_mode != 'average':
                    row['distance_passed'] = distance_error <= plan['distance_abs']
                report['results'].append(row)
        gl.DeleteProgram(program)
        gl.DeleteTextures(1, c.byref(tex))
        for buf in [inp, out, localout]:
            gl.DeleteBuffers(1, c.byref(buf))
        report['gl_error'] = int(gl.GetError())
        report['passed'] = report['gl_error'] == 0 and all(x['normal_passed'] and x['mapping_passed'] and x.get('distance_passed', True) for x in report['results'])
    finally:
        gl.close()
        (dest/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
