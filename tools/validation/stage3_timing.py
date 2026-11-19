"""Paired GPU pass timing on identical frozen inputs; no production math changes."""
import argparse
import ctypes as c
import hashlib
import json
import random
import time
from pathlib import Path
import numpy as np
from run import GL, ROOT, SHADERS, U, I, read_shader
from stage2 import Pipeline


def stats(values):
    return dict(median=float(np.median(values)),p10=float(np.percentile(values,10)),p90=float(np.percentile(values,90)))


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--regression',type=Path,required=True)
    parser.add_argument('--contact',type=Path,required=True)
    parser.add_argument('--settled',type=Path,required=True)
    args=parser.parse_args();dest=args.output;dest.mkdir(parents=True,exist_ok=False)
    fixtures={'initial':args.regression/'initialization/cube.bin','contact':args.contact,'settled':args.settled}
    modes={'3-off':(3,False,False),'8-off':(8,False,False),'8-gpu':(8,True,False),'8-sync':(8,True,True)}
    plan=dict(particles=10000,grid=128,dt=.00025,blocks=5,steps_per_block=10,warmup=5,
              variants=modes,seed=0,inputs={k:str(v) for k,v in fixtures.items()},
              frozen=True,swap_buffers=False,rendering=False,evaluation=False,history=False,
              vsync='no swap',sdf='outside domain',
              gpu_timing='GL_TIME_ELAPSED for each dispatch + production pass barrier; query readback after batch',
              wall_timing='Python/ctypes submission plus final GPU completion, excluding query collection; not application FPS',
              validity='3 sweeps is a counterfactual performance baseline; known insufficient accuracy, not a supported simulation')
    (dest/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    paths=[Path(__file__),ROOT/'tools/validation/stage2.py',ROOT/'tools/validation/run.py',*fixtures.values(),
           *[SHADERS/('mpm_'+s+'_3dtex.comp') for s in ['setup','p2g','grid','g2p']]]
    report=dict(plan=plan,results=[],blocks=[],hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
    physics=np.fromfile(args.regression/'initialization/physics.bin',np.float32).reshape(2,28)[0]
    gl=GL(dest);report['gpu']=gl.info
    try:
        for name,ret,signature in [('GenQueries',None,[I,c.POINTER(U)]),('DeleteQueries',None,[I,c.POINTER(U)]),
                                   ('BeginQuery',None,[U,U]),('EndQuery',None,[U]),
                                   ('GetQueryObjectui64v',None,[U,U,c.POINTER(c.c_uint64)])]:
            setattr(gl,name,c.WINFUNCTYPE(ret,*signature)(gl.glfw.glfwGetProcAddress(('gl'+name).encode())))
        rng=random.Random(plan['seed'])
        for fixture,path in fixtures.items():
            folder=dest/fixture;folder.mkdir();gl.output=folder
            pipe=Pipeline(gl,physics);particles=np.fromfile(path,np.float32).reshape(-1,48);pipe.upload(particles)
            old={}
            for i,name in [(1,'p2g'),(3,'g2p')]:
                source=read_shader((SHADERS/('mpm_'+name+'_3dtex.comp')))
                if source.count('iter < 8')!=1:raise RuntimeError('Jacobi anchor changed')
                old[i]=gl.program(source.replace('iter < 8','iter < 3'))
            original=pipe.programs.copy()
            queries=(U*(plan['steps_per_block']*4))();gl.GenQueries(len(queries),queries)
            def configure(mode):
                sweeps,validation,sync=modes[mode]
                pipe.programs=original.copy()
                if sweeps==3:
                    for i in [1,3]:pipe.programs[i]=old[i]
                for i in [1,3]:
                    program=pipe.programs[i];gl.UseProgram(program)
                    gl.Uniform1i(gl.GetUniformLocation(program,b'validation_enabled'),int(validation))
                zero=np.zeros(24,np.uint32);gl.BindBuffer(0x90D2,pipe.fault)
                gl.BufferSubData(0x90D2,0,zero.nbytes,zero.ctypes.data)
                for binding,buffer in [(0,pipe.buffers[0]),(1,pipe.buffers[1]),(6,pipe.fault)]:gl.BindBufferBase(0x90D2,binding,buffer)
                gl.BindBufferBase(0x8A11,1,pipe.ubo)
            def step(mode,index=None):
                if modes[mode][2]:
                    zero=np.zeros(24,np.uint32);gl.BindBuffer(0x90D2,pipe.fault)
                    gl.BufferSubData(0x90D2,0,zero.nbytes,zero.ctypes.data)
                for i,program in enumerate(pipe.programs):
                    gl.UseProgram(program)
                    if index is not None:gl.BeginQuery(0x88BF,queries[index*4+i])
                    gl.DispatchCompute(*((16,16,16) if i in [0,2] else ((len(particles)+63)//64,1,1)))
                    gl.MemoryBarrier(0x2000|1 if i==3 else 0x20)
                    if index is not None:gl.EndQuery(0x88BF)
                    if i in [1,3] and modes[mode][2]:
                        gl.MemoryBarrier(0x2000|0x200)
                        if gl.read(pipe.fault,(24,)).view(np.uint32)[0]:raise RuntimeError('Unexpected deformation fault')
            samples={mode:[] for mode in modes};wall={mode:[] for mode in modes}
            for block in range(plan['blocks']):
                order=list(modes);rng.shuffle(order)
                for mode in order:
                    configure(mode)
                    for _ in range(plan['warmup']):step(mode)
                    gl.Finish();start=time.perf_counter()
                    for index in range(plan['steps_per_block']):step(mode,index)
                    gl.Finish();elapsed=(time.perf_counter()-start)*1000/plan['steps_per_block']
                    values=[]
                    for query in queries:
                        ns=c.c_uint64();gl.GetQueryObjectui64v(query,0x8866,c.byref(ns));values.append(ns.value/1e6)
                    values=np.array(values).reshape(-1,4);samples[mode].extend(values.tolist());wall[mode].append(elapsed)
                    gl.MemoryBarrier(0x2000|0x200)
                    if modes[mode][1] and gl.read(pipe.fault,(24,)).view(np.uint32)[0]:raise RuntimeError('Unexpected GPU validation fault')
                    report['blocks'].append(dict(fixture=fixture,block=block,mode=mode,wall_ms=elapsed,gpu_ms=values.tolist()))
                    print(fixture,block,mode,round(elapsed,3),flush=True)
            for mode in modes:
                values=np.array(samples[mode]);report['results'].append(dict(fixture=fixture,mode=mode,
                    gpu_ms={name:stats(values[:,i]) for i,name in enumerate(['Setup','P2G','Grid','G2P'])},
                    gpu_total_ms=stats(values.sum(axis=1)),wall_ms=stats(wall[mode])))
            pipe.programs=original;pipe.close()
            for program in old.values():gl.DeleteProgram(program)
            gl.DeleteQueries(len(queries),queries)
        report['gl_error']=int(gl.GetError());report['passed']=report['gl_error']==0
    finally:
        gl.close();(dest/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
