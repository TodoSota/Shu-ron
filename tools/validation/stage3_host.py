"""Build and exercise the actual C++ simulator's stop/save/reset lifecycle."""
import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
from run import ROOT


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ssbo',action='store_true');p.add_argument('--linear',action='store_true');p.add_argument('--build-template',type=Path,required=True);args=p.parse_args()
    dest=args.output.resolve();dest.mkdir(parents=True,exist_ok=False)
    source=r'''
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include "src/mpm/3dtexture/MpmSimulator3DTEX.h"
#include "src/mpm/MpmParticle.h"
#include "src/sdf/SdfInstance.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool condition){if(!condition)throw std::runtime_error("Host lifecycle assertion failed");}
MpmParticle read(GLuint buffer){MpmParticle p{};glBindBuffer(GL_ARRAY_BUFFER,buffer);
glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(p),&p);return p;}
int main(){
 if(!glfwInit())return 2;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
 glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
 glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
 auto* w=glfwCreateWindow(32,32,"Host lifecycle probe",nullptr,nullptr);if(!w)return 3;
 glfwMakeContextCurrent(w);glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)return 4;
 while(glGetError()!=GL_NO_ERROR){}
 try {
  auto mesh=std::make_shared<MeshResource>("src/assets/object.obj");mesh->generateSDF(17);
  SdfInstance obstacle(mesh);obstacle.position=glm::vec3(10);obstacle.updateMatrices();
  MpmSimulator3DTEX sim(1,128,1.5f,false);
  MpmPhysics physics{};physics.timestep=.00025f;physics.dx=1.5f/128;physics.inv_dx=128/1.5f;
  physics.f_normal=glm::vec3(0,1,0);physics.f_height=-10;physics.p_vol=2.7e-6f;
  physics.p_mass=.00108f;physics.p_mu=50000/2.6f;physics.p_lambda=50000*.3f/(1.3f*.4f);
  sim.resetParticles(.1f,false);sim.setPhysics(physics);sim.step(obstacle);
  require(sim.getSimulationError().empty()&&sim.getStepCount()==1);
  for(int stage:{1,2}) {
   sim.resetParticles(.1f,false);
   auto buffer=sim.getMpmObject().getReadVbo();auto particle=read(buffer);
   if(stage==1)particle.deformation[0][0]=-1;
   else particle.affineC[0][0]=-8000;
   glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(particle),&particle);
   sim.step(obstacle);auto after=read(buffer);
   require(!sim.getSimulationError().empty()&&sim.getSimulationError().find("saved to")!=std::string::npos);
   require(sim.getSimulationError().find("stage="+std::to_string(stage))!=std::string::npos);
   require(sim.getStepCount()==0&&sim.getMpmObject().getReadVbo()==buffer);
   require(std::memcmp(&particle,&after,sizeof(particle))==0);
   sim.step(obstacle);require(sim.getStepCount()==0);
  }
  sim.resetParticles(.1f,false);sim.step(obstacle);
  require(sim.getSimulationError().empty()&&sim.getStepCount()==1);
  require(glGetError()==GL_NO_ERROR);
  std::cout<<"HOST_LIFECYCLE_PASS"<<std::endl;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 5;}
 glfwDestroyWindow(w);glfwTerminate();return 0;
}
'''
    sources=['src/mpm/IMpmSimulator.cpp','src/mpm/IMpmObject.cpp','src/mpm/MpmEvaluator.cpp',
             'src/mpm/3dtexture/MpmSimulator3DTEX.cpp','src/mpm/3dtexture/MpmObject3DTEX.cpp','src/core/Shader.cpp']
    if args.ssbo:
        source=source.replace('3dtexture/MpmSimulator3DTEX','ssbo/MpmSimulatorSSBO')
        source=source.replace('MpmSimulator3DTEX sim','MpmSimulatorSSBO sim')
        sources[3:5]=['src/mpm/ssbo/MpMSimulatorSSBO.cpp','src/mpm/ssbo/MpmObjectSSBO.cpp']
    if args.linear:
        source=source.replace('128,1.5f,false)', '128,1.5f,true)')
        start=source.index('  sim.resetParticles')
        end=source.index(' }catch',start)
        source=source[:start]+r"""
  sim.resetParticles(.1f,false);
  sim.setPhysics(physics);
  const auto buffer=sim.getMpmObject().getReadVbo();
  auto initial=read(buffer);
  initial.position=glm::vec4(glm::vec3(64.25f,64.5f,64.75f)*physics.dx,1);
  initial.velocity=glm::vec4(.2f,-.1f,.3f,0);
  glBindBuffer(GL_ARRAY_BUFFER,buffer);
  glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(initial),&initial);
  sim.step(obstacle);
  const auto nextBuffer=sim.getMpmObject().getReadVbo();
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  const auto next=read(nextBuffer);
  const glm::vec3 expected=glm::vec3(initial.position)+glm::vec3(initial.velocity)*physics.timestep;
  const float positionError=glm::length(glm::vec3(next.position)-expected);
  const float velocityError=glm::length(glm::vec3(next.velocity)-glm::vec3(initial.velocity));
  require(sim.getSimulationError().empty() && sim.getStepCount()==1 && nextBuffer!=buffer);
  require(positionError<=2e-7f && velocityError<=1e-6f && glGetError()==GL_NO_ERROR);

  // Isolate transfer from deformation and measure the current linear C coefficient.
  physics.timestep=0;
  sim.setPhysics(physics);
  initial.affineC[0][0]=.4f;
  glBindBuffer(GL_ARRAY_BUFFER,nextBuffer);
  glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(initial),&initial);
  sim.step(obstacle);
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  const float recovered=read(sim.getMpmObject().getReadVbo()).affineC[0][0];
  const float expectedAffine=.4f;
  require(sim.getStepCount()==2 && std::abs(recovered-expectedAffine)<3e-5f && glGetError()==GL_NO_ERROR);
  std::cout<<"LINEAR_CONNECTION_PASS position_error="<<positionError
           <<" velocity_error="<<velocityError<<" recovered_affine="<<recovered
           <<" expected_affine="<<expectedAffine<<" GPU="<<glGetString(GL_RENDERER)<<std::endl;
  physics.timestep=.00025f;
"""+source[start:end].replace("auto particle=read(buffer);","auto particle=read(buffer);\n   particle.position=initial.position;")+source[end:]
    (dest/'probe.cpp').write_text(source,encoding='utf-8')
    command=args.build_template.read_text(encoding='utf-8').replace('probe.cpp /Fe:',
        'probe.cpp /DGLM_ENABLE_EXPERIMENTAL '+' '.join('"'+str(ROOT/f)+'"' for f in sources)+' /Fe:')
    (dest/'build.cmd').write_text(command,encoding='utf-8')
    built=subprocess.run(['cmd','/c','build.cmd'],cwd=dest,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (dest/'build.log').write_bytes(built.stdout)
    if built.returncode:raise RuntimeError('Host probe build failed')
    shutil.copyfile(args.build_template.parent/'glew32.dll',dest/'glew32.dll')
    result=subprocess.run([str(dest/'probe.exe')],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    (dest/'probe.log').write_bytes(result.stdout)
    created={ROOT/path.decode('ascii') for path in
             re.findall(rb'\(saved to (artifacts/diagnostics/C9-\d+-\d+)\)',result.stdout)}
    passed = (b'LINEAR_CONNECTION_PASS' in result.stdout and b'HOST_LIFECYCLE_PASS' in result.stdout and len(created)==2 if args.linear else
              b'HOST_LIFECYCLE_PASS' in result.stdout and len(created)==2)
    report=dict(passed=result.returncode==0 and passed,
                exit_code=result.returncode,diagnostic_directories=[str(x) for x in sorted(created)],
                hashes={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sources})
    suffix='ssbo' if args.ssbo else '3dtex'
    shader_dir=ROOT/'src/mpm/shaders'/('ssbo' if args.ssbo else '3dtexture')
    for stage in ('setup','p2g','grid','g2p'):
        linear=args.linear and stage in ('p2g','g2p')
        shader=shader_dir/f'mpm_{stage}{"_Linear" if linear else ""}_{suffix}.comp'
        report['hashes'][shader.relative_to(ROOT).as_posix()]=hashlib.sha256(shader.read_bytes()).hexdigest()
    for folder in created:
        for name in ['fault.bin','previous-particles.bin','physics.bin','diagnostic.txt']:
            report['passed'] &= (folder/name).exists()
    (dest/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
