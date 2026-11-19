"""Continuous production TEX quadratic pipeline; requires a fresh stage1 report."""
import argparse
import ctypes as c
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
from run import GL, U, I, P, ROOT, SHADERS, read_shader

# Written to plan.json before dispatch. Limits are not fitted to observed results.
PLAN = dict(grid=128, world=1.5, duration=1.0, dts=[.001, .0005, .00025],
            analytic_position=2e-4, analytic_velocity=2e-3,
            dt_position_rms=.02, dt_centroid=.01, dt_aabb=.03,
            mass_relative=3e-6, settling_speed=.1,
            stop='nonfinite, detF<=0, support outside grid, GL error',
            sdf='identity transform; AABB [10,11]^3 excludes every grid node',
            rendering=False, evaluation=False, history=False, swap_buffers=False)


def shader_filename(stage, suffix, linear):
    infix = '_Linear' if linear and stage in ('p2g', 'g2p') else ''
    return f'mpm_{stage}{infix}_{suffix}.comp'


class Pipeline:
    def __init__(self, gl, physics, ssbo=False, linear=False):
        self.gl = gl
        self.ssbo = ssbo
        directory = SHADERS.parent / 'ssbo' if ssbo else SHADERS
        suffix = 'ssbo' if ssbo else '3dtex'
        self.programs = [gl.program(read_shader(directory / shader_filename(s, suffix, linear)))
                         for s in ['setup', 'p2g', 'grid', 'g2p']]
        self.ubo = gl.buffer(physics, 1, 0x8A11)
        self.fault = gl.buffer(np.zeros(24,np.uint32),6)
        self.textures = (U*4)()
        if ssbo:
            self.grid = gl.buffer(np.zeros((128,128,128,4),np.float32),2)
            for program in self.programs:
                gl.UseProgram(program)
                gl.set_grid_size(program,128)
        else:
            gl.GenTextures(4, self.textures)
            for i, tex in enumerate(self.textures):
                gl.BindTexture(0x806F, tex)
                gl.TexParameteri(0x806F, 0x2801, 0x2600)
                gl.TexParameteri(0x806F, 0x2800, 0x2600)
                gl.TexImage3D(0x806F, 0, 0x822E, 128, 128, 128, 0, 0x1903, 0x1406, None)
                gl.BindImageTexture(i, tex, 0, 1, 0, 0x88BA, 0x8236)
        for name, ret, args in [('GetUniformLocation', I, [U,c.c_char_p]),
                                 ('Uniform1i', None, [I,I]),
                                 ('BufferSubData', None, [U,c.c_ssize_t,c.c_ssize_t,P]),
                                 ('Uniform3f', None, [I,c.c_float,c.c_float,c.c_float]),
                                 ('UniformMatrix4fv', None, [I,I,U,P])]:
            addr=gl.glfw.glfwGetProcAddress(('gl'+name).encode())
            setattr(gl,name,c.WINFUNCTYPE(ret,*args)(addr))
        for program in [self.programs[1],self.programs[3]]:
            gl.UseProgram(program)
            gl.Uniform1i(gl.GetUniformLocation(program,b'validation_enabled'),1)
        grid = self.programs[2]
        gl.UseProgram(grid)
        identity=np.eye(4,dtype=np.float32)
        for name in ['sdf_model','sdf_inv_model']:
            gl.UniformMatrix4fv(gl.GetUniformLocation(grid,name.encode()),1,0,identity.ctypes.data)
        for name, value in [('sdf_aabb_min',10),('sdf_aabb_max',11)]:
            gl.Uniform3f(gl.GetUniformLocation(grid,name.encode()),value,value,value)

    def upload_grid(self, fields):
        gl = self.gl
        if self.ssbo:
            data = np.ascontiguousarray(np.moveaxis(fields, 0, -1), np.float32)
            gl.BindBuffer(0x90D2, self.grid)
            gl.BufferSubData(0x90D2, 0, data.nbytes, data.ctypes.data)
        else:
            for tex, data in zip(self.textures, fields):
                gl.BindTexture(0x806F, tex)
                gl.TexImage3D(0x806F, 0, 0x822E, 128, 128, 128, 0, 0x1903, 0x1406, data.ctypes.data)

    def read_grid(self):
        gl = self.gl
        if self.ssbo:
            gl.MemoryBarrier(0x200)
            return np.moveaxis(gl.read(self.grid, (128,128,128,4)), -1, 0)
        gl.MemoryBarrier(0x100)
        fields = np.empty((4,128,128,128), np.float32)
        for tex, data in zip(self.textures, fields):
            gl.BindTexture(0x806F, tex)
            gl.GetTexImage(0x806F, 0, 0x1903, 0x1406, data.ctypes.data)
        return fields

    def upload(self, particles):
        self.shape=particles.shape
        self.buffers=[self.gl.buffer(particles,0),self.gl.buffer(np.zeros_like(particles),1)]

    def step(self):
        gl=self.gl
        zero=np.zeros(24,np.uint32)
        gl.BindBuffer(0x90D2,self.fault)
        gl.BufferSubData(0x90D2,0,zero.nbytes,zero.ctypes.data)
        gl.BindBufferBase(0x90D2,6,self.fault)
        gl.BindBufferBase(0x90D2,0,self.buffers[0])
        gl.BindBufferBase(0x90D2,1,self.buffers[1])
        for i, program in enumerate(self.programs):
            gl.UseProgram(program)
            gl.DispatchCompute(*((16,16,16) if i in [0,2] else ((self.shape[0]+63)//64,1,1)))
            gl.MemoryBarrier(0x2000|0x1 if i==3 else (0x2000 if self.ssbo else 0x20))
            if i in [1,3]:
                gl.MemoryBarrier(0x2000|0x200)
                fault=gl.read(self.fault,(24,)).view(np.uint32)
                if fault[0]:
                    fault.tofile(gl.output/'deformation-fault.bin')
                    gl.read(self.buffers[0],self.shape).tofile(gl.output/'deformation-previous.bin')
                    raise RuntimeError(f'Deformation fault stage={fault[3]} particle={fault[1]} reason={fault[2]}')
        self.buffers.reverse()

    def read(self):
        # Buffer readback has its own visibility requirement, additional to app barriers.
        self.gl.MemoryBarrier(0x200)
        self.gl.Finish()
        error=self.gl.GetError()
        if error:
            raise RuntimeError(f'GL error {error:#x}')
        return self.gl.read(self.buffers[0],self.shape)

    def close(self):
        for b in getattr(self,'buffers',[])+[self.ubo,self.fault]:
            self.gl.DeleteBuffers(1,c.byref(b))
        if self.ssbo:
            self.gl.DeleteBuffers(1,c.byref(self.grid))
        else:
            self.gl.DeleteTextures(4,self.textures)
        for p in self.programs:
            self.gl.DeleteProgram(p)


def metrics(a, linear=False):
    x=a[:,:3].astype(float)
    f=a[:,24:40].reshape(-1,4,4).transpose(0,2,1)[:,:3,:3].astype(float)
    det=np.linalg.det(f)
    base=np.floor(x/(1.5/128)-(0. if linear else .5))
    return dict(finite=bool(np.isfinite(a[:,:43]).all()),det_min=float(np.min(det)),
                support_outside=int(np.count_nonzero(np.any((base<0)|(base+(1 if linear else 2)>=128),axis=1))),
                centroid=x.mean(axis=0).tolist(),aabb=[x.min(axis=0).tolist(),x.max(axis=0).tolist()],
                max_speed=float(np.linalg.norm(a[:,4:7],axis=1).max()),
                rms_speed=float(np.sqrt(np.mean(np.sum(a[:,4:7]**2,axis=1)))))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--regression',type=Path,required=True)
    parser.add_argument('--dts',type=float,nargs='+',default=PLAN['dts'])
    parser.add_argument('--fall-only',action='store_true')
    parser.add_argument('--initial-shape',choices=['cube','sphere'],default='cube')
    parser.add_argument('--ssbo',action='store_true')
    parser.add_argument('--linear',action='store_true')
    args=parser.parse_args()
    PLAN['linear']=args.linear
    PLAN['storage']='ssbo' if args.ssbo else '3dtex'
    shader_dir=SHADERS.parent / 'ssbo' if args.ssbo else SHADERS
    if any(dt<=0 or abs(round(PLAN['duration']/dt)*dt-PLAN['duration'])>1e-9 for dt in args.dts):
        parser.error('Each dt must be positive and divide duration exactly')
    PLAN['dts']=args.dts
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'plan.json').write_text(json.dumps(PLAN,indent=2),encoding='utf-8')
    (args.output/'stage2.py').write_bytes(Path(__file__).read_bytes())
    regression=json.loads((args.regression/'report.json').read_text(encoding='utf-8'))
    if not regression['passed']:
        raise RuntimeError('Stage1 regression must pass')
    for name, digest in regression['hashes'].items():
        path=SHADERS/name if (SHADERS/name).exists() else ROOT/name
        if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Stale regression: '+name)
    cube=np.fromfile(args.regression/('initialization/'+args.initial_shape+'.bin'),np.float32).reshape(-1,48)
    physics=np.fromfile(args.regression/'initialization/physics.bin',np.float32).reshape(2,28)[0]
    if args.initial_shape=='sphere':
        physics=np.fromfile(args.regression/'initialization/shape-physics.bin',np.float32).reshape(5,28)[0]
    PLAN['initial_shape']=args.initial_shape
    report=dict(plan=PLAN,regression=str(args.regression),cases=[],
                hashes={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in [Path(__file__),ROOT/'tools/validation/run.py',
                                  *[shader_dir/shader_filename(s, PLAN["storage"], args.linear) for s in ['setup','p2g','grid','g2p']]]})
    (args.output/'source.diff').write_bytes(subprocess.check_output(['git','diff','--binary'],cwd=ROOT))
    gl=None
    try:
        gl=GL(args.output)
        report['gpu']=gl.info
        print(json.dumps(gl.info),flush=True)
        final={}
        cases=[] if args.fall_only else [('advection',.001,.05),('gravity',.001,.05)]
        cases += [('fall',dt,PLAN['duration']) for dt in PLAN['dts']]
        for kind,dt,duration in cases:
            label=f'{kind}-{dt:g}'
            initial=cube.copy()
            param=physics.copy(); param[3]=dt
            if kind=='advection':
                param[:3]=0
                initial[:,4:7]=[.2,-.1,.3]
            pipe=Pipeline(gl,param,ssbo=args.ssbo,linear=args.linear)
            pipe.upload(initial)
            result=dict(name=label,dt=dt,duration=duration,trace=[])
            report['cases'].append(result)
            previous=initial
            for step in range(1,round(duration/dt)+1):
                pipe.step()
                current=pipe.read()
                m=metrics(current, args.linear)
                if step==1 or step%max(1,round(.05/dt))==0:
                    result['trace'].append(dict(step=step,time=step*dt,**m))
                    print(label,step,m['max_speed'],m['det_min'],flush=True)
                if not m['finite'] or m['det_min']<=0 or m['support_outside']:
                    result['failure']=dict(step=step,time=step*dt,**m)
                    previous.tofile(args.output/(label+'-previous.bin'))
                    current.tofile(args.output/(label+'-failure.bin'))
                    break
                previous=current
            current.tofile(args.output/(label+'-final.bin'))
            result['final']=metrics(current, args.linear)
            result['completed_steps']=step
            result['passed']='failure' not in result
            if kind!='fall' and result['passed']:
                acceleration=param[:3].astype(float)
                expected_x=initial[:,:3]+initial[:,4:7]*duration+acceleration*(dt*dt*step*(step+1)/2)
                expected_v=initial[:,4:7]+acceleration*duration
                result['position_error']=float(np.max(np.linalg.norm(current[:,:3]-expected_x,axis=1)))
                result['velocity_error']=float(np.max(np.linalg.norm(current[:,4:7]-expected_v,axis=1)))
                result['passed']=result['position_error']<=PLAN['analytic_position'] and result['velocity_error']<=PLAN['analytic_velocity']
            if kind=='fall' and result['passed']:
                result['settled']=result['final']['rms_speed']<=PLAN['settling_speed']
                final[dt]=current.copy()
            pipe.close()
        report['dt_comparisons']=[]
        for coarse,fine in zip(PLAN['dts'][:-1],PLAN['dts'][1:]):
            if coarse in final and fine in final:
                a,b=final[coarse][:,:3].astype(float),final[fine][:,:3].astype(float)
                comparison=dict(coarse=coarse,fine=fine,
                                position_rms=float(np.sqrt(np.mean(np.sum((a-b)**2,axis=1)))),
                                centroid=float(np.linalg.norm(a.mean(0)-b.mean(0))),
                                aabb=float(np.max(np.abs(np.array([a.min(0),a.max(0)])-np.array([b.min(0),b.max(0)])))))
                comparison['passed']=all(comparison[k]<=PLAN['dt_'+k] for k in ['position_rms','centroid','aabb'])
                report['dt_comparisons'].append(comparison)
        report['passed']=all(x['passed'] for x in report['cases']) and len(final)==len(PLAN['dts']) and all(x['passed'] for x in report['dt_comparisons']) and all(x.get('settled',True) for x in report['cases'])
    except Exception as exc:
        report['error']=repr(exc); report['passed']=False
        raise
    finally:
        if gl:
            gl.close()
        (args.output/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
