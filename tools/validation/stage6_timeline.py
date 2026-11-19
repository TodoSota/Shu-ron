"""Exercise history capacity, fallback, ring restoration, and shared shader loading."""
import argparse
import json
import shutil
import subprocess
from pathlib import Path

from run import ROOT

SOURCE = r"""
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "src/core/Shader.h"
#include "src/mpm/TimelineManager.h"
#include "src/mpm/MpmParticle.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

PFNGLBUFFERDATAPROC realBufferData;
void GLAPIENTRY limitBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    realBufferData(target, size > 1000 ? -1 : size, data, usage);
}

void verifyTimeline() {
    {
        TimelineManager capped(600,10000);
        GLint64 blockLimit = 0;
        glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE,&blockLimit);
        const auto budget = blockLimit < 128LL*1024*1024 ? blockLimit : 128LL*1024*1024;
        check(capped.getCapacity() == budget/(10000*sizeof(MpmParticle)), "capacity");
        std::cout << "capacity=" << capped.getCapacity() << '\n';
    }
    TimelineManager disabled(0,2);
    disabled.recordFrame(0,nullptr);
    check(disabled.getCapacity()==0 && disabled.getCurrentFrameCount()==0,"disabled history");

    realBufferData=glBufferData;
    __glewBufferData=limitBufferData;
    TimelineManager ring(8,2);
    __glewBufferData=realBufferData;
    check(ring.getCapacity()==2,"allocation fallback");
    SdfInstance obstacle(nullptr);
    std::array<MpmParticle,2> particles{};
    GLuint buffer=0;
    glGenBuffers(1,&buffer);
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,sizeof(particles),nullptr,GL_DYNAMIC_DRAW);
    auto setFrame = [&](int frame) {
        for (auto& particle : particles) {
            particle.position=glm::vec4(float(frame),2,3,1);
            particle.velocity=glm::vec4(4,5,6,0);
            particle.affineC=glm::mat4(float(frame));
            particle.deformation=glm::mat4(.875f);
            particle.alpha=.25f;
            particle.q=.125f;
            particle.vc=.0625f;
            particle.state=frame;
        }
    };
    for (int frame=1;frame<=3;++frame) {
        setFrame(frame);
        obstacle.position.x=float(frame);
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(particles),particles.data());
        ring.recordFrame(buffer,&obstacle);
    }
    check(ring.getCurrentFrameCount()==2 && ring.getPlaybackFrame()==1,"ring wrap");
    for (int logical=0;logical<2;++logical) {
        setFrame(logical+2);
        ring.restoreToFrame(logical,buffer,&obstacle);
        std::array<MpmParticle,2> actual{};
        glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(actual),actual.data());
        check(std::memcmp(actual.data(),particles.data(),sizeof(actual))==0,"particle restore");
        check(obstacle.position.x==logical+2,"obstacle restore");
    }
    glDeleteBuffers(1,&buffer);
}

void verifyShaderLoading(const std::string& badShader) {
    for (const auto* path : {"src/render/shaders/dust_emit.comp","src/render/shaders/density_splat.comp",
                             "src/render/shaders/surfel_prepare.comp","src/mpm/shaders/mpm_evaluator.comp"}) {
        auto program=loadCompute(path);
        check(program!=0,path);
        glDeleteProgram(program);
    }
    for (const auto* path : {"src/render/point","src/render/shaders/sphere","src/render/shaders/splat"}) {
        std::string source(path);
        auto program=loadProgram(source+".vert",source+".frag");
        check(program!=0,path);
        glDeleteProgram(program);
    }
    check(loadCompute(badShader)==0,"included compile error");
}

int main(int argc, char** argv) {
    if (argc!=2 || !glfwInit()) return 2;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(32,32,"Timeline probe",nullptr,nullptr);
    if (!window) return 3;
    glfwMakeContextCurrent(window);
    glewExperimental=GL_TRUE;
    if (glewInit()!=GLEW_OK) return 4;
    while (glGetError()!=GL_NO_ERROR) {}
    try {
        verifyTimeline();
        verifyShaderLoading(argv[1]);
        check(glGetError()==GL_NO_ERROR,"GL error");
        std::cout << "TIMELINE_PASS GPU=" << glGetString(GL_RENDERER) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 5;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-template', type=Path, required=True)
    args = parser.parse_args()
    dest = args.output.resolve()
    dest.mkdir(parents=True, exist_ok=False)
    (dest/'probe.cpp').write_text(SOURCE, encoding='utf-8')
    (dest/'bad.comp').write_text('#version 430 core\n#include "bad.glsl"\nvoid main() {}\n')
    (dest/'bad.glsl').write_text('int invalid = ;\n')
    sources = [ROOT/'src/core/shader.cpp', ROOT/'src/mpm/TimelineManager.cpp']
    compile_args = 'probe.cpp /DGLM_ENABLE_EXPERIMENTAL ' + ' '.join(f'"{src}"' for src in sources)
    command = args.build_template.read_text(encoding='utf-8').replace('probe.cpp /Fe:',compile_args+' /Fe:')
    (dest/'build.cmd').write_text(command,encoding='utf-8')
    compiled = subprocess.run(['cmd','/c','build.cmd'],cwd=dest,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (dest/'build.log').write_bytes(compiled.stdout)
    if compiled.returncode:
        raise RuntimeError('Timeline probe build failed')
    shutil.copyfile(args.build_template.parent/'glew32.dll',dest/'glew32.dll')
    run = subprocess.run([str(dest/'probe.exe'),str(dest/'bad.comp')],cwd=ROOT,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
    (dest/'probe.log').write_bytes(run.stdout)
    passed = run.returncode == 0 and b'TIMELINE_PASS' in run.stdout and b'bad.glsl' in run.stdout
    (dest/'report.json').write_text(json.dumps(dict(passed=passed,exit_code=run.returncode),indent=2))
    return 0 if passed else 1


if __name__=='__main__':
    raise SystemExit(main())

