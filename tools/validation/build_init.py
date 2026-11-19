"""Build a probe from current C++ initialization bodies, using real GL buffers."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path
import subprocess
from run import ROOT, section


def build(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    main = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
    simulator = (ROOT/'src/mpm/IMpmSimulator.cpp').read_text(encoding='utf-8')
    constants = section(main, 'int currentParticleCount{', '/// シミュレーターを生成する関数')
    # Exclude the commented-out E alternative by beginning at the active declaration.
    physics = section(main, '\tconst float E = 5e4f;', '\t// 4. 描画状態の初期設定')
    reset = section(simulator, 'void IMpmSimulator::resetParticles(', '/// 物理パラメータ(UBO)')
    set_physics = simulator[simulator.index('void IMpmSimulator::setPhysics('):]
    source = r'''
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "src/mpm/MpmParticle.h"
#include "src/mpm/MpmPhysics.h"
#include <random>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstddef>
static_assert(sizeof(MpmParticle)==192);
static_assert(offsetof(MpmParticle,alpha)==160);
static_assert(offsetof(MpmParticle,scale)==176);
static_assert(sizeof(MpmPhysics)==112);
struct Object {
 GLuint vbo[2]{};
 GLuint getReadVbo(){return vbo[0];}
 GLuint getWriteVbo(){return vbo[1];}
 int getParticleCount(){return 10000;}
};
// Only ownership/evaluator scaffolding is substituted. The reset body is verbatim.
struct Evaluator {void clearLogs(){}};
struct IMpmSimulator {
 Object* mpmObject; Evaluator evaluator; float worldScale=1.5f; int stepCount=0;
 GLuint physicsUbo=0; MpmPhysics requestedPhysics{};
 bool hasPhysics=false, sphereParticles=false; float sphereParticleVolume=0;
 std::string simulationError;
 void resetParticles(float scale,bool sphere);
 void setPhysics(const MpmPhysics& physics);
};
@CONSTANTS@
@RESET@
@SET_PHYSICS@
void dump(const char* path,GLuint buf){
 std::vector<MpmParticle> data(10000);
 glBindBuffer(GL_ARRAY_BUFFER,buf);
 glGetBufferSubData(GL_ARRAY_BUFFER,0,data.size()*sizeof(MpmParticle),data.data());
 std::ofstream f(path,std::ios::binary); f.write((char*)data.data(),data.size()*sizeof(MpmParticle));
 if(!f) throw std::runtime_error("Output failed");
}
int main(){
 if(!glfwInit())return 2;
 glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
 glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
 glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
 auto* win=glfwCreateWindow(32,32,"Initialization probe",nullptr,nullptr);
 if(!win)return 3;
 glfwMakeContextCurrent(win); glewExperimental=GL_TRUE;
 if(glewInit()!=GLEW_OK)return 4;
 while(glGetError()!=GL_NO_ERROR){}
 std::cout<<"GPU "<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<std::endl;
 Object obj; glGenBuffers(2,obj.vbo);
 std::vector<MpmParticle> zero(10000);
 for(auto buf:obj.vbo){glBindBuffer(GL_ARRAY_BUFFER,buf);glBufferData(GL_ARRAY_BUFFER,zero.size()*sizeof(MpmParticle),zero.data(),GL_DYNAMIC_DRAW);}
 IMpmSimulator sim{&obj};
 glGenBuffers(1,&sim.physicsUbo); glBindBuffer(GL_UNIFORM_BUFFER,sim.physicsUbo);
 glBufferData(GL_UNIFORM_BUFFER,sizeof(MpmPhysics),nullptr,GL_DYNAMIC_DRAW);
 sim.resetParticles(1,false); dump("cube.bin",obj.vbo[0]); dump("cube-write.bin",obj.vbo[1]);
 sim.resetParticles(1,false); dump("cube-repeat.bin",obj.vbo[0]);
 sim.resetParticles(1,true); dump("sphere.bin",obj.vbo[0]); dump("sphere-write.bin",obj.vbo[1]);
 sim.resetParticles(1,true); dump("sphere-repeat.bin",obj.vbo[0]);
 sim.resetParticles(.25f,true); dump("sphere-small.bin",obj.vbo[0]);
 @PHYSICS@
 auto restart=resetMpmSimParam(mpmSimParam,128);
 std::ofstream params("physics.bin",std::ios::binary);
 params.write((char*)&mpmSimParam,sizeof(mpmSimParam)); params.write((char*)&restart,sizeof(restart));
 std::ofstream shapes("shape-physics.bin",std::ios::binary);
 auto record=[&](){MpmPhysics effective{}; glBindBuffer(GL_UNIFORM_BUFFER,sim.physicsUbo);
 glGetBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(effective),&effective);
 shapes.write((char*)&effective,sizeof(effective));};
 // Physics after reset, reset after physics, repeated upload, and density changes.
 sim.resetParticles(1,true); sim.setPhysics(mpmSimParam); record();
 sim.setPhysics(mpmSimParam); record();
 sim.resetParticles(.25f,true); record();
 sim.resetParticles(1,false); record();
 auto dense=mpmSimParam; dense.p_mass*=2;
 sim.setPhysics(dense); sim.resetParticles(1,true); record();
 glDeleteBuffers(1,&sim.physicsUbo);
 glFinish(); auto err=glGetError(); std::cout<<"GL_ERROR "<<err<<std::endl;
 glDeleteBuffers(2,obj.vbo);glfwDestroyWindow(win);glfwTerminate();return err?5:0;
}
'''
    source = source.replace('@CONSTANTS@',constants).replace('@RESET@',reset).replace('@PHYSICS@',physics).replace('@SET_PHYSICS@',set_physics)
    (output/'probe.cpp').write_text(source,encoding='utf-8')
    glew = ROOT/'packages/glew-2.2.0.2.2.0.1/build/native'
    glfw = ROOT/'packages/glfw.3.4.0/build/native'
    glm = ROOT/'packages/glm.1.0.2/build/native/include'
    vcvars = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
    paths = [ROOT,output,vcvars]
    if any(any(ch in str(p) for ch in '%!\r\n') for p in paths):
        raise ValueError('Unsupported cmd path characters')
    command = f'@call "{vcvars}"\n@if errorlevel 1 exit /b 1\n@cl /nologo /MD /EHsc /std:c++17 /utf-8 /Od /I"{ROOT}" /I"{glew}/include" /I"{glfw}/include" /I"{glm}" probe.cpp /Fe:probe.exe /link "{glew}/lib/Release/x64/glew32.lib" "{glfw}/lib/static/v143/x64/glfw3.lib" opengl32.lib user32.lib gdi32.lib shell32.lib\n'
    (output/'build.cmd').write_text(command,encoding='utf-8')
    result = subprocess.run(['cmd','/c','build.cmd'],cwd=output,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'build.log').write_bytes(result.stdout)
    if result.returncode:
        raise RuntimeError('Probe build failed; see build.log')
    shutil.copyfile(glew/'bin/Release/x64/glew32.dll',output/'glew32.dll')
    files=['src/main.cpp','src/mpm/IMpmSimulator.cpp','src/mpm/MpmParticle.h','src/mpm/MpmPhysics.h',
           'tools/validation/build_init.py']
    manifest={str(path):hashlib.sha256((ROOT/path).read_bytes()).hexdigest() for path in files}
    manifest['probe.exe']=hashlib.sha256((output/'probe.exe').read_bytes()).hexdigest()
    manifest['probe.cpp']=hashlib.sha256((output/'probe.cpp').read_bytes()).hexdigest()
    manifest['glew32.dll']=hashlib.sha256((output/'glew32.dll').read_bytes()).hexdigest()
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(output/'probe.exe')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--output',required=True,type=Path)
    build(p.parse_args().output)
