"""Windows x64 / repository GLFW / NumPy. No application source is modified."""
import argparse
import re
import ctypes as c
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SHADERS = ROOT / 'src/mpm/shaders/3dtexture'
# Fixed before GPU execution. Dimensionless SVD residuals use Frobenius norms.
TOL = dict(svd=5e-4, transfer=3e-5, stress=5e-4, mass=3e-6, init=2e-6)
U = c.c_uint
I = c.c_int
P = c.c_void_p
S = c.c_ssize_t


_SHADER_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"\s*$')


def read_shader(path, stack=()):
    path = path.resolve()
    if path in stack or len(stack) >= 32:
        raise RuntimeError(f'Recursive shader include: {path}')
    lines = []
    for line in path.read_text(encoding='utf-8').splitlines(keepends=True):
        match = _SHADER_INCLUDE.fullmatch(line)
        if match:
            lines.append(read_shader(path.parent/match[1], (*stack, path)))
        else:
            lines.append(line if line.endswith('\n') else line+'\n')
    return ''.join(lines)



class GL:
    def __init__(self, output):
        self.output=output
        self.program_count=0
        dll = ROOT / 'packages/glfw.3.4.0/build/native/bin/dynamic/v143/x64/glfw3.dll'
        self.glfw = g = c.CDLL(str(dll))
        g.glfwInit.restype = I
        g.glfwWindowHint.argtypes = [I, I]
        g.glfwCreateWindow.argtypes = [I, I, c.c_char_p, P, P]
        g.glfwCreateWindow.restype = P
        g.glfwMakeContextCurrent.argtypes = [P]
        g.glfwDestroyWindow.argtypes = [P]
        g.glfwGetProcAddress.argtypes = [c.c_char_p]
        g.glfwGetProcAddress.restype = P
        if not g.glfwInit():
            raise RuntimeError('glfwInit failed')
        for key, value in [(0x00020004, 0), (0x00022002, 4), (0x00022003, 3),
                           (0x00022008, 0x00032001)]:
            g.glfwWindowHint(key, value)
        self.window = g.glfwCreateWindow(32, 32, b'MPM numerical validation', None, None)
        if not self.window:
            raise RuntimeError('OpenGL 4.3 context creation failed')
        g.glfwMakeContextCurrent(self.window)
        specs = {
            'GetString': (c.c_char_p, [U]), 'GetError': (U, []),
            'CreateShader': (U, [U]), 'ShaderSource': (None, [U, I, c.POINTER(c.c_char_p), P]),
            'CompileShader': (None, [U]), 'GetShaderiv': (None, [U, U, c.POINTER(I)]),
            'GetShaderInfoLog': (None, [U, I, P, P]), 'CreateProgram': (U, []),
            'AttachShader': (None, [U, U]), 'LinkProgram': (None, [U]),
            'GetProgramiv': (None, [U, U, c.POINTER(I)]),
            'GetProgramInfoLog': (None, [U, I, P, P]), 'DeleteShader': (None, [U]),
            'UseProgram': (None, [U]), 'DeleteProgram': (None, [U]),
            'GenBuffers': (None, [I, c.POINTER(U)]), 'BindBuffer': (None, [U, U]),
            'BufferData': (None, [U, S, P, U]), 'BindBufferBase': (None, [U, U, U]),
            'GetBufferSubData': (None, [U, S, S, P]),
            'DeleteBuffers': (None, [I, c.POINTER(U)]),
            'GenTextures': (None, [I, c.POINTER(U)]), 'BindTexture': (None, [U, U]),
            'TexParameteri': (None, [U, U, I]),
            'TexImage3D': (None, [U, I, I, I, I, I, I, U, U, P]),
            'BindImageTexture': (None, [U, U, I, U, I, U, U]),
            'GetTexImage': (None, [U, I, U, U, P]),
            'DeleteTextures': (None, [I, c.POINTER(U)]),
            'DispatchCompute': (None, [U, U, U]), 'MemoryBarrier': (None, [U]),
            'Finish': (None, []),
        }
        for name, (ret, args) in specs.items():
            addr = g.glfwGetProcAddress(('gl' + name).encode())
            if not addr:
                raise RuntimeError('Missing GL entry point: ' + name)
            setattr(self, name, c.WINFUNCTYPE(ret, *args)(addr))
        self.info = {name: self.GetString(enum).decode() for name, enum in
                     [('vendor', 0x1F00), ('renderer', 0x1F01), ('gl', 0x1F02), ('glsl', 0x8B8C)]}

    def close(self):
        self.glfw.glfwMakeContextCurrent(None)
        self.glfw.glfwDestroyWindow(self.window)
        self.glfw.glfwTerminate()

    def program(self, source):
        self.program_count+=1
        (self.output/f'shader-{self.program_count:02}.comp').write_text(source,encoding='utf-8')
        shader = self.CreateShader(0x91B9)
        encoded = c.c_char_p(source.encode())
        self.ShaderSource(shader, 1, c.byref(encoded), None)
        self.CompileShader(shader)
        ok = I()
        self.GetShaderiv(shader, 0x8B81, c.byref(ok))
        if not ok.value:
            log = c.create_string_buffer(32768)
            self.GetShaderInfoLog(shader, len(log), None, log)
            raise RuntimeError(log.value.decode())
        program = self.CreateProgram()
        self.AttachShader(program, shader)
        self.LinkProgram(program)
        self.GetProgramiv(program, 0x8B82, c.byref(ok))
        if not ok.value:
            log = c.create_string_buffer(32768)
            self.GetProgramInfoLog(program, len(log), None, log)
            raise RuntimeError(log.value.decode())
        self.DeleteShader(shader)
        return program

    def buffer(self, data, binding, target=0x90D2):
        data = np.ascontiguousarray(data)
        buf = U()
        self.GenBuffers(1, c.byref(buf))
        self.BindBuffer(target, buf)
        self.BufferData(target, data.nbytes, data.ctypes.data, 0x88E8)
        self.BindBufferBase(target, binding, buf)
        return buf

    def read(self, buf, shape):
        data = np.empty(shape, np.float32)
        self.BindBuffer(0x90D2, buf)
        self.GetBufferSubData(0x90D2, 0, data.nbytes, data.ctypes.data)
        return data

    def set_grid_size(self, program, size):
        location = c.WINFUNCTYPE(I, U, c.c_char_p)(self.glfw.glfwGetProcAddress(b'glGetUniformLocation'))
        uniform = c.WINFUNCTYPE(None, I, I)(self.glfw.glfwGetProcAddress(b'glUniform1i'))
        uniform(location(program, b'u_gridSize'), size)

    def dispatch(self, program, count):
        self.UseProgram(program)
        self.DispatchCompute((count + 63) // 64, 1, 1)
        self.MemoryBarrier(0xFFFFFFFF)
        self.Finish()
        err = self.GetError()
        if err:
            raise RuntimeError(f'GL error: {err:#x}')


def section(text, start, end):
    # Fail closed if the source structure changes; never silently test stale copies.
    if text.count(start) != 1 or text.count(end) != 1:
        raise RuntimeError('Source extraction anchor changed: ' + start)
    return text[text.index(start):text.index(end)]


def relative(actual, expected):
    return float(np.linalg.norm(actual - expected) / max(1.0, np.linalg.norm(expected)))


class Suite:
    def __init__(self, gl):
        self.gl = gl
        self.results = []

    def check(self, name, error, limit, expected_failure=False):
        finite = bool(np.isfinite(error))
        passed = bool(finite and (error > limit if expected_failure else error <= limit))
        self.results.append(dict(name=name, error=float(error), limit=limit,
                                 expected_failure=expected_failure, passed=passed))
        print(f'{"PASS" if passed else "FAIL"} {name}: {error:.6g} / {limit:.6g}', flush=True)

    def svd(self, source, label, mutation=False):
        functions = section(source, 'void jacobiEigen(', 'void main()')
        if mutation:
            functions = functions.replace('atan(-2.0 * apq', 'atan(2.0 * apq')
        shader = '''#version 430
layout(local_size_x=64) in;
layout(std430,binding=4) readonly buffer Input { mat4 fs[]; };
layout(std430,binding=5) writeonly buffer Output { mat4 outdata[]; };
''' + functions + '''
void main() {
 uint i=gl_GlobalInvocationID.x; if(i>=fs.length()) return;
 mat3 u,s,v; svd(mat3(fs[i]),u,s,v);
 outdata[3*i]=mat4(u); outdata[3*i+1]=mat4(s); outdata[3*i+2]=mat4(v);
}'''
        rng = np.random.default_rng(0)
        a = .61
        rot = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
        matrices = [np.eye(3), np.diag([.7, 1.1, 1.4]), rot,
                    np.array([[1, .3, 0], [0, 1, .2], [0, 0, 1]])]
        matrices += list(np.eye(3) + .1 * rng.normal(size=(128, 3, 3)))
        # C-9 normal contact trial that exceeded the orthogonality tolerance
        # with only three Jacobi sweeps; keep the tolerance unchanged.
        matrices += [np.array([[.971635,.09727411,.00863537],[-.07745893,.8388049,-.39469293],[-.06326719,.5084884,.8885082]]),
                     rot @ np.diag([.2,1.8,3.]), np.array([[1.,.8,.2],[0,1.,.7],[0,0,1.]])]
        # Clamp / inversion are diagnostic cases, not claimed supported material states.
        matrices += [np.diag([1, 1, 1e-5]), np.diag([1, 1, -1])]
        fs = np.tile(np.eye(4, dtype=np.float32), (len(matrices), 1, 1))
        fs[:, :3, :3] = matrices
        inp = self.gl.buffer(fs.transpose(0, 2, 1).copy(), 4)
        out = self.gl.buffer(np.zeros((len(fs)*3, 4, 4), np.float32), 5)
        program = self.gl.program(shader)
        self.gl.dispatch(program, len(fs))
        raw = self.gl.read(out, (len(fs), 3, 4, 4)).transpose(0, 1, 3, 2)
        errs = dict(reconstruction=[], orthogonality=[], diagonalization=[], singular_values=[])
        for f, (u4, s4, v4) in zip(fs[:, :3, :3], raw):
            u, s, v = u4[:3, :3].astype(float), s4[:3, :3].astype(float), v4[:3, :3].astype(float)
            errs['reconstruction'].append(relative(u @ s @ v.T, f))
            errs['orthogonality'].append(max(relative(u.T @ u, np.eye(3)), relative(v.T @ v, np.eye(3))))
            d = v.T @ f.T @ f @ v
            errs['diagonalization'].append(relative(d - np.diag(np.diag(d)), np.zeros((3, 3))))
            errs['singular_values'].append(relative(np.sort(np.diag(s)), np.sort(np.linalg.svd(f, compute_uv=False))))
        if mutation:
            self.check(label + '/old-angle-detected', max(errs['orthogonality'][:-2]), TOL['svd'], True)
        else:
            for metric, values in errs.items():
                self.check(label + '/' + metric, max(values[:-2]), TOL['svd'])
            for i, name in [(-2, 'clamp'), (-1, 'inversion')]:
                self.check(label + '/' + name + '/finite', 0 if np.isfinite(raw[i]).all() else np.inf, 0)
                self.results.append(dict(name=label + '/' + name + '/diagnostic',
                                         metrics={k: v[i] for k, v in errs.items()},
                                         det_u=float(np.linalg.det(raw[i, 0, :3, :3]))))
        self.gl.DeleteProgram(program)
        for b in [inp, out]:
            self.gl.DeleteBuffers(1, c.byref(b))

    def stress(self, source, mutation=False):
        functions = section(source, 'void jacobiEigen(', 'void main()')
        body = section(source, '\tmat3 U, sig, V;', '\t// MLS-MPM の力の項:')
        if mutation:
            body = body.replace('transpose(U)', 'transpose(V)') + '\nstress *= transpose(F);\n'
        shader = '''#version 430
layout(local_size_x=64) in;
layout(std430,binding=4) readonly buffer Input { mat4 fs[]; };
layout(std430,binding=5) writeonly buffer Output { mat4 stresses[]; };
const float p_mu=19230.76923076923, p_lambda=28846.153846153844;
''' + functions + '\nvoid main(){ uint idx=gl_GlobalInvocationID.x; if(idx>=fs.length())return; mat3 F=mat3(fs[idx]);\n' + body + '\nstresses[idx]=mat4(stress); }'
        a = .61
        r = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
        fs = np.tile(np.eye(4, dtype=np.float32), (5, 1, 1))
        fs[:, :3, :3] = [np.eye(3), r, np.diag([.8, .9, 1.1]), r @ np.diag([.8, .9, 1.1]),
                          np.array([[1, .3, 0], [0, 1, .2], [0, 0, 1]])]
        inp = self.gl.buffer(fs.transpose(0, 2, 1).copy(), 4)
        out = self.gl.buffer(np.zeros_like(fs), 5)
        program = self.gl.program(shader)
        self.gl.dispatch(program, len(fs))
        actual = self.gl.read(out, fs.shape).transpose(0, 2, 1)[:, :3, :3]
        errors = []
        for f, tau in zip(fs[:, :3, :3].astype(float), actual):
            # Independent symmetric spectral log of B=F F^T, no shader SVD reuse.
            values, vectors = np.linalg.eigh(f @ f.T)
            logv = .5 * np.log(values)
            expected = vectors @ np.diag(2*(50000/2.6)*logv + (50000*.3/(1.3*.4))*sum(logv)) @ vectors.T
            errors.append(float(np.linalg.norm(tau-expected) / max(50000, np.linalg.norm(expected))))
        self.check('stress/' + ('old-form-detected' if mutation else 'spectral-reference'),
                   max(errors), TOL['stress'], mutation)
        self.gl.DeleteProgram(program)
        for b in [inp, out]:
            self.gl.DeleteBuffers(1, c.byref(b))

    def plastic(self, source):
        # Keep production declarations, SVD and the entire material update verbatim;
        # only replace grid gathering with C=0, v=0 to isolate constitutive response.
        shader = section(source, '#version 430 core', 'void main()')
        shader += '''void main(){ uint idx=gl_GlobalInvocationID.x;
if(idx>=particleIn.length())return; particleOut[idx]=particleIn[idx];
mat3 new_C=mat3(0); vec3 new_v=vec3(0);
''' + source[source.index('\tmat3 F_new ='):]
        fs = [np.eye(3), .9*np.eye(3), 1.1*np.eye(3), np.diag(np.exp([.2,-.2,0])),
              np.diag(np.exp([-.01,-.02,-.01]))]
        particles = np.zeros((len(fs),48),np.float32)
        for row, f in zip(particles,fs):
            f4=np.eye(4); f4[:3,:3]=f
            row[24:40]=f4.T.ravel(); row[40]=.267765
        physics=np.zeros(28,np.float32)
        physics[14:16]=50000/2.6,50000*.3/(1.3*.4)
        ubo=self.gl.buffer(physics,1,0x8A11)
        inp=self.gl.buffer(particles,0)
        out=self.gl.buffer(np.zeros_like(particles),1)
        program=self.gl.program(shader)
        self.gl.dispatch(program,len(fs))
        actual=self.gl.read(out,particles.shape)
        for i, name in enumerate(['identity','compression','tension','shear','elastic']):
            f=particles[i,24:40].reshape(4,4).T[:3,:3].astype(float)
            u,s,vt=np.linalg.svd(f)
            eps=np.log(s); tr=sum(eps); dev=eps-tr/3; norm=np.linalg.norm(dev)
            gamma=norm+(3*float(physics[15])+2*float(physics[14]))/(2*float(physics[14]))*tr*float(particles[i,40])
            if tr>0:
                state=0; projected=np.zeros(3); q=np.linalg.norm(eps)
            elif gamma<=0:
                state=1; projected=eps; q=0
            else:
                state=2; projected=eps-gamma*dev/norm; q=gamma
            expected=u@np.diag(np.exp(projected))@vt
            got=actual[i,24:40].reshape(4,4).T[:3,:3]
            self.check('plastic/'+name+'/F',relative(got,expected),TOL['svd'])
            phi=np.deg2rad(35+(9*q-10)*np.exp(-.2*q))
            alpha=np.sqrt(2/3)*2*np.sin(phi)/(3-np.sin(phi))
            vc=np.log(np.linalg.det(f))-np.log(np.linalg.det(expected))
            self.check('plastic/'+name+'/q-alpha-vc',relative(actual[i,40:43],[alpha,q,vc]),TOL['init'])
            got_state=actual[i].view(np.int32)[43]
            self.check('plastic/'+name+'/state',abs(int(got_state)-state),0)
        self.gl.DeleteProgram(program)
        for buf in [ubo,inp,out]:
            self.gl.DeleteBuffers(1,c.byref(buf))

    def initialization(self, probe, output):
        manifest=json.loads((probe.parent/'manifest.json').read_text(encoding='utf-8'))
        for path, expected in manifest.items():
            candidate=probe.parent/path if path in ['probe.exe','probe.cpp','glew32.dll'] else ROOT/path
            if hashlib.sha256(candidate.read_bytes()).hexdigest()!=expected:
                raise RuntimeError('Stale initialization probe: '+path)
        dest=output/'initialization'
        dest.mkdir()
        (dest/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
        (dest/'probe.cpp').write_bytes((probe.parent/'probe.cpp').read_bytes())
        result=subprocess.run([str(probe.resolve())],cwd=dest,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
        (dest/'probe.log').write_bytes(result.stdout)
        if result.returncode:
            raise RuntimeError('Initialization probe failed; see probe.log')
        cube=np.fromfile(dest/'cube.bin',np.float32).reshape(10000,48)
        self.check('init/cube-finite',0 if np.isfinite(cube[:,:43]).all() else np.inf,0)
        self.check('init/cube-bounds',float(np.maximum(.6-cube[:,:3],0).sum()+np.maximum(cube[:,:3]-.9,0).sum()),0)
        for name in ['cube-write','cube-repeat']:
            # Equality is appropriate for an unchanged buffer copy / fixed CPU RNG.
            self.check('init/'+name,0 if (dest/'cube.bin').read_bytes()==(dest/(name+'.bin')).read_bytes() else 1,0)
        self.check('init/velocity-C',float(np.max(abs(cube[:,4:24]))),0)
        self.check('init/F',relative(cube[:,24:40],np.tile(np.eye(4).ravel(),(10000,1))),0)
        self.check('init/alpha',float(np.max(abs(cube[:,40]-.267765))),TOL['init'])
        self.check('init/q-vc',float(np.max(abs(cube[:,41:43]))),0)
        self.check('init/state',float(np.max(abs(cube.view(np.int32)[:,43]-1))),0)
        params=np.fromfile(dest/'physics.bin',np.float32).reshape(2,28)
        self.check('init/volume-total',abs(float(params[0,13])*10000-.027)/.027,TOL['init'])
        self.check('init/mass-total',abs(float(params[0,16])*10000-10.8)/10.8,TOL['init'])
        self.check('init/start-restart',relative(params[0],params[1]),TOL['init'])
        self.check('init/contact-radius',abs(float(params[0,17])-1.5/128/2),TOL['init'])
        sphere=np.fromfile(dest/'sphere.bin',np.float32).reshape(10000,48)
        bad=int((~np.isfinite(sphere[:,:3]).all(axis=1)).sum())
        self.check('init/sphere-finite',bad,0)
        for name in ['sphere-write','sphere-repeat']:
            self.check('init/'+name,0 if (dest/'sphere.bin').read_bytes()==(dest/(name+'.bin')).read_bytes() else 1,0)
        # Independent uniform-ball CDF P(R<=r)=r^3; fixed, conservative
        # statistical bounds declared before execution, N=10000, seed=0.
        for name, radius in [('sphere',.15),('sphere-small',.0375)]:
            rows=np.fromfile(dest/(name+'.bin'),np.float32).reshape(10000,48)
            xyz=(rows[:,:3].astype(float)-.75)/radius
            radii=np.linalg.norm(xyz,axis=1)
            self.check('init/'+name+'/finite',0 if np.isfinite(rows[:,:43]).all() else np.inf,0)
            self.check('init/'+name+'/radius',float(np.maximum(radii-1,0).max()),TOL['init'])
            self.check('init/'+name+'/mean',float(np.max(abs(xyz.mean(axis=0)))),.025)
            self.check('init/'+name+'/second-moment',float(np.max(abs((xyz*xyz).mean(axis=0)-.2))),.015)
            cdf=np.sort(radii)**3
            ks=max(np.max(np.arange(1,10001)/10000-cdf),np.max(cdf-np.arange(10000)/10000))
            self.check('init/'+name+'/radial-cdf',float(ks),.025)
        shape_physics=np.fromfile(dest/'shape-physics.bin',np.float32).reshape(5,28)
        sphere_volume=4*np.pi/3*.15**3/10000
        expected_volumes=np.array([sphere_volume,sphere_volume,sphere_volume*.25**3,.027/10000,sphere_volume])
        self.check('init/shape-volume',float(np.max(abs(shape_physics[:,13]/expected_volumes-1))),TOL['init'])
        expected_masses=expected_volumes*np.array([400,400,400,400,800])
        self.check('init/shape-mass',float(np.max(abs(shape_physics[:,16]/expected_masses-1))),TOL['init'])
        self.check('init/sphere-domain',int(((sphere[:,:3]<0)|(sphere[:,:3]>1.5)).any(axis=1).sum()),0)
        self.check('init/shape-other-physics',relative(np.delete(shape_physics,[13,16],axis=1),np.tile(np.delete(params[0],[13,16]),(5,1))),TOL['init'])
        self.results.append(dict(name='init/diagnostic',sphere_nonfinite=bad,
                                 sphere_outside_domain=int(((sphere[:,:3]<0)|(sphere[:,:3]>1.5)).any(axis=1).sum()),
                                 mean_particles_per_cell=10000/(.3/(1.5/128))**3))

    def transfer(self, p2g, g2p, mutation=False, ssbo=False):
        gl = self.gl
        if mutation:
            g2p = g2p.replace('4.0 * inv_dx * inv_dx * weight', '4.0 * inv_dx * weight')
        pp, gp = gl.program(p2g), gl.program(g2p)
        n, dx = 16, 1.5 / 128
        physics = np.zeros(28, np.float32)
        physics[10:12] = dx, 1/dx
        physics[13:17] = 2.7e-6, 50000/2.6, 50000*.3/(1.3*.4), .00108
        ubo = gl.buffer(physics, 1, 0x8A11)  # dt=0 isolates transfer from material evolution.
        textures = (U*4)()
        grid = None
        if ssbo:
            grid = gl.buffer(np.zeros((n,n,n,4),np.float32), 2)
            for program in (pp, gp):
                gl.UseProgram(program)
                gl.set_grid_size(program, n)
        else:
            gl.GenTextures(4, textures)
        coords = np.indices((n, n, n))[::-1].transpose(1, 2, 3, 0) * dx
        xs = np.array([[6.1, 6.7, 7.2], [6.6, 7.3, 7.8], [7.4, 6.2, 6.9]], np.float32)*dx
        matrices = [np.zeros((3, 3)), np.array([[.3, .1, -.2], [.2, -.4, .1], [.1, 0, .2]]),
                    np.array([[0, -2, 0], [2, 0, 0], [0, 0, 0]])]
        for kind, a in zip(['constant', 'affine', 'rotation'], matrices):
            b = np.array([.2, -.1, .3])
            particles = np.zeros((len(xs), 48), np.float32)  # 192-byte production layout
            particles[:, :3], particles[:, 3] = xs, 1
            particles[:, 4:7] = xs @ a.T + b
            particles[:, 8:24] = np.array(np.pad(a, ((0,1),(0,1))).T, np.float32).ravel()
            particles[:, 24:40] = np.eye(4, dtype=np.float32).ravel()
            particles[:, 40] = .267765
            inp = gl.buffer(particles, 0)
            out = gl.buffer(np.zeros_like(particles), 1)
            def upload(fields):
                if ssbo:
                    data = np.ascontiguousarray(np.stack(fields, axis=-1), np.float32)
                    gl.BindBuffer(0x90D2, grid)
                    gl.BufferData(0x90D2, data.nbytes, data.ctypes.data, 0x88E8)
                    return
                for i, field in enumerate(fields):
                    data = np.ascontiguousarray(field, np.float32)
                    gl.BindTexture(0x806F, textures[i])
                    gl.TexParameteri(0x806F, 0x2801, 0x2600)
                    gl.TexParameteri(0x806F, 0x2800, 0x2600)
                    gl.TexImage3D(0x806F, 0, 0x822E, n, n, n, 0, 0x1903, 0x1406, data.ctypes.data)
                    gl.BindImageTexture(i, textures[i], 0, 1, 0, 0x88BA, 0x8236)
            zeros = np.zeros((n,n,n), np.float32)
            upload([zeros]*4)
            gl.dispatch(pp, len(xs))  # Production P2G, unmodified, including CAS.
            if ssbo:
                gl.MemoryBarrier(0x200)
                fields = np.moveaxis(gl.read(grid, (n,n,n,4)), -1, 0).astype(float)
            else:
                fields = []
                for tex in textures:
                    data = np.empty_like(zeros)
                    gl.BindTexture(0x806F, tex)
                    gl.GetTexImage(0x806F, 0, 0x1903, 0x1406, data.ctypes.data)
                    fields.append(data.astype(float))
            mass = fields[3].sum()
            # Independent nodal reference using the centered piecewise basis N(|r|).
            ref_mass=np.zeros_like(zeros,dtype=float)
            ref_momentum=np.zeros((*zeros.shape,3),dtype=float)
            for row in particles:
                r=np.abs((coords-row[:3])/dx)
                weights=np.prod(np.where(r<.5,.75-r*r,np.where(r<1.5,.5*(1.5-r)**2,0)),axis=-1)
                ref_mass+=float(physics[16])*weights
                ref_momentum+=float(physics[16])*weights[...,None]*(row[4:7]+(coords-row[:3])@a.T)
            self.check(f'p2g/{kind}/mass', abs(mass-len(xs)*physics[16])/(len(xs)*physics[16]), TOL['mass'])
            momentum = np.stack(fields[:3], axis=-1)
            self.check(f'p2g/{kind}/nodal-mass',float(np.linalg.norm(fields[3]-ref_mass)/np.linalg.norm(ref_mass)),TOL['transfer'])
            self.check(f'p2g/{kind}/nodal-momentum',float(np.linalg.norm(momentum-ref_momentum)/np.linalg.norm(ref_momentum)),TOL['transfer'])
            expected_p=physics[16]*particles[:,4:7].sum(axis=0)
            self.check(f'p2g/{kind}/momentum', float(np.linalg.norm(momentum.sum(axis=(0,1,2))-expected_p)/np.linalg.norm(expected_p)), TOL['transfer'])
            # APIC intrinsic angular momentum: m dx^2/4 * axial(C-C^T).
            intrinsic = np.array([a[2,1]-a[1,2], a[0,2]-a[2,0], a[1,0]-a[0,1]])*dx*dx/4
            expected_l = physics[16]*(np.cross(xs, particles[:,4:7]).sum(axis=0) + len(xs)*intrinsic)
            self.check(f'p2g/{kind}/angular', float(np.linalg.norm(np.cross(coords, momentum).sum(axis=(0,1,2))-expected_l)/np.linalg.norm(expected_l)), TOL['transfer'])
            # Analytic grid field isolates G2P from P2G mass threshold / occupancy.
            velocity = coords @ a.T + b
            upload([velocity[...,i] for i in range(3)] + [np.ones_like(zeros)])
            gl.dispatch(gp, len(xs))
            actual = gl.read(out, particles.shape)
            self.check(f'g2p/{kind}/velocity', relative(actual[:,4:7], xs @ a.T+b), TOL['transfer'])
            cs = actual[:,8:24].reshape(-1,4,4).transpose(0,2,1)[:,:3,:3]
            error = relative(cs, np.broadcast_to(a, cs.shape))
            self.check(f'g2p/{kind}/' + ('old-C-detected' if mutation and kind!='constant' else 'C'),
                       error, TOL['transfer'], mutation and kind!='constant')
            if not mutation:
                self.check(f'g2p/{kind}/alpha-q0', float(np.max(abs(actual[:,40]-.267765))), TOL['init'])
                self.check(f'g2p/{kind}/finite', 0 if np.isfinite(actual).all() else np.inf, 0)
            for buf in [inp,out]:
                gl.DeleteBuffers(1,c.byref(buf))
        gl.DeleteBuffers(1,c.byref(ubo))
        if ssbo:
            gl.DeleteBuffers(1,c.byref(grid))
        else:
            gl.DeleteTextures(4,textures)
        gl.DeleteProgram(pp)
        gl.DeleteProgram(gp)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--init-probe', type=Path, required=True)
    parser.add_argument('--ssbo', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    directory = SHADERS.parent / 'ssbo' if args.ssbo else SHADERS
    suffix = 'ssbo' if args.ssbo else '3dtex'
    sources = {name: read_shader(directory/name) for name in
               [f'mpm_{stage}_{suffix}.comp' for stage in ('p2g','g2p')]}
    report = dict(utc=datetime.now(timezone.utc).isoformat(), tolerances=TOL,
                  python=sys.version, numpy=np.__version__,
                  head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  status=subprocess.check_output(['git','status','--short'],cwd=ROOT,text=True),
                  hashes={str((directory/name).relative_to(ROOT)) if args.ssbo else name:
                          hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in sources})
    for path in ['tools/validation/run.py','tools/validation/build_init.py','src/main.cpp',
                 'src/mpm/IMpmSimulator.cpp','src/mpm/MpmParticle.h','src/mpm/MpmPhysics.h']:
        report['hashes'][path]=hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    for path in sorted((SHADERS.parent/'common').glob('*.glsl')):
        report['hashes'][path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (args.output/'source.diff').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=ROOT))
    gl = None
    try:
        gl = GL(args.output)
        report['gpu'] = gl.info
        print(json.dumps(gl.info), flush=True)
        suite = Suite(gl)
        report['results'] = suite.results
        p2g,g2p = sources.values()
        suite.svd(p2g, 'p2g-svd')
        suite.svd(g2p, 'g2p-svd')
        suite.svd(p2g, 'negative-control', True)
        suite.stress(p2g)
        suite.stress(p2g, True)
        suite.plastic(g2p)
        suite.transfer(p2g,g2p,ssbo=args.ssbo)
        suite.transfer(p2g,g2p,True,ssbo=args.ssbo)
        suite.initialization(args.init_probe,args.output.resolve())
        report['passed'] = all(r.get('passed',True) for r in suite.results)
    except Exception as exc:
        report['passed'] = False
        report['error'] = repr(exc)
        raise
    finally:
        if gl:
            gl.close()
        (args.output/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
