"""Compile real MeshResource SDF generation; compare voxel samples to box-union geometry."""
import argparse
import hashlib
import json
import shutil
import subprocess
from pathlib import Path
import numpy as np
from run import ROOT


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--build-template', type=Path, required=True)
    args = p.parse_args()
    dest = args.output.resolve(); dest.mkdir(parents=True, exist_ok=False)
    # Boundary quads of three unit cubes in an L, plus a convex box.
    fixtures = {}
    for name, cells in [('box', {(0, 0, 0)}), ('concave', {(0, 0, 0), (1, 0, 0), (0, 1, 0)})]:
        rectangles = []
        triangles = []
        for cell in sorted(cells):
            cell = np.array(cell)
            for axis in range(3):
                other = [i for i in range(3) if i != axis]
                for sign in [-1, 1]:
                    neighbor = cell.copy(); neighbor[axis] += sign
                    if tuple(neighbor) in cells: continue
                    lo = cell.astype(float); hi = lo+1
                    lo[axis] = hi[axis] = cell[axis]+(sign > 0)
                    corners = []
                    for a, b in [(0, 0), (1, 0), (1, 1), (0, 1)]:
                        v = lo.copy(); v[other] = [lo[other[0]]+a, lo[other[1]]+b]
                        corners.append(v)
                    if np.cross(corners[1]-corners[0], corners[2]-corners[0])[axis]*sign < 0:
                        corners.reverse()
                    rectangles.append((lo, hi))
                    triangles += [[corners[i] for i in ids] for ids in [(0, 1, 2), (0, 2, 3)]]
        fixtures[name] = dict(cells=cells, rectangles=rectangles, triangles=triangles)
    fixtures['reversed'] = dict(fixtures['box'], triangles=[t[::-1] for t in fixtures['box']['triangles']])
    fixtures['open'] = dict(fixtures['box'], triangles=fixtures['box']['triangles'][2:])
    fixtures['degenerate'] = dict(fixtures['box'], triangles=fixtures['box']['triangles']+[[np.zeros(3)]*3])
    fixtures['local_winding'] = dict(fixtures['box'], triangles=[fixtures['box']['triangles'][0][::-1]]+fixtures['box']['triangles'][1:])
    fixtures['nonmanifold'] = dict(fixtures['box'], triangles=fixtures['box']['triangles']+fixtures['box']['triangles'][:1])
    for name, fixture in fixtures.items():
        lines = []
        for i, triangle in enumerate(fixture['triangles']):
            lines += ['v '+' '.join(str(float(v)) for v in vertex) for vertex in triangle]
            lines.append(f'f {3*i+1} {3*i+2} {3*i+3}')
        (dest/(name+'.obj')).write_text('\n'.join(lines)+'\n', encoding='ascii')
    shutil.copyfile(ROOT/'src/assets/object.obj',dest/'asset.obj')
    source = r'''
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#define TINYOBJLOADER_IMPLEMENTATION
#include "src/core/MeshResource.h"
#include <fstream>
int main(){
 if(!glfwInit())return 2;
 glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
 glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
 glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
 auto* w=glfwCreateWindow(32,32,"SDF generation probe",nullptr,nullptr);if(!w)return 3;
 glfwMakeContextCurrent(w);glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK)return 4;
 while(glGetError()!=GL_NO_ERROR){}
 std::cout<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<std::endl;
 for(auto name:{"box","concave","reversed","open","degenerate","local_winding","nonmanifold","asset","fallback"}){
  try {
  MeshResource mesh(std::string(name)+".obj");mesh.generateSDF(17);
  std::vector<float> sdf(17*17*17);glBindTexture(GL_TEXTURE_3D,mesh.sdfTexture3D);
  glGetTexImage(GL_TEXTURE_3D,0,GL_RED,GL_FLOAT,sdf.data());
  std::ofstream f(std::string(name)+".bin",std::ios::binary);
  f.write((char*)&mesh.aabbMin,sizeof(glm::vec3));f.write((char*)&mesh.aabbMax,sizeof(glm::vec3));
  f.write((char*)sdf.data(),sdf.size()*sizeof(float));
  } catch(const std::exception& e) {std::ofstream f(std::string(name)+"-rejected.txt");f<<e.what();}
 }
 auto err=glGetError();glfwDestroyWindow(w);glfwTerminate();return err?5:0;
}
'''
    (dest/'probe.cpp').write_text(source, encoding='utf-8')
    (dest/'build.cmd').write_bytes(args.build_template.read_bytes())
    result = subprocess.run(['cmd', '/c', 'build.cmd'], cwd=dest, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (dest/'build.log').write_bytes(result.stdout)
    if result.returncode: raise RuntimeError('Mesh probe build failed')
    shutil.copyfile(args.build_template.parent/'glew32.dll', dest/'glew32.dll')
    plan = dict(resolution=17, distance_abs=2e-6,
                reference='minimum distance to exposed axis-aligned rectangles, membership in box union',
                supported=['box', 'concave', 'reversed'], rejected=['open', 'degenerate', 'local_winding', 'nonmanifold'],
                asset='finite and accepted', fallback_sphere_abs=.006)
    (dest/'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
    result = subprocess.run([str(dest/'probe.exe')], cwd=dest, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    (dest/'probe.log').write_bytes(result.stdout)
    if result.returncode: raise RuntimeError('Mesh probe failed')
    report = dict(results=[], hashes={})
    for path in [ROOT/'src/core/MeshResource.h', Path(__file__), dest/'probe.cpp', dest/'probe.exe', *dest.glob('*.obj')]:
        report['hashes'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name, fixture in fixtures.items():
        rejection=dest/(name+'-rejected.txt')
        if name in plan['rejected']:
            report['results'].append(dict(name=name,rejected=rejection.exists(),passed=rejection.exists()))
            continue
        if rejection.exists():
            report['results'].append(dict(name=name,passed=False,error=rejection.read_text()))
            continue
        raw = np.fromfile(dest/(name+'.bin'), np.float32)
        lo, hi = raw[:3], raw[3:6]
        z, y, x = np.meshgrid(*[np.linspace(lo[i], hi[i], 17) for i in [2, 1, 0]], indexing='ij')
        points = np.stack([x, y, z], axis=-1)
        distance = np.full(x.shape, np.inf)
        for lower, upper in fixture['rectangles']:
            distance = np.minimum(distance, np.linalg.norm(points-np.clip(points, lower, upper), axis=-1))
        # Samples on shared interior faces need union membership; use a small
        # offset-independent closed membership and exclude surface-distance=0.
        inside = np.zeros(x.shape, bool)
        for cell in fixture['cells']:
            inside |= ((points >= np.array(cell)) & (points <= np.array(cell)+1)).all(axis=-1)
        expected = np.where(inside, -distance, distance)
        actual = raw[6:].reshape(x.shape)
        mask = distance > plan['distance_abs']
        row = dict(name=name, nonfinite=int((~np.isfinite(actual)).sum()),
                   sign_errors=int(((actual < 0) != inside)[mask].sum()),
                   max_distance_error=float(np.max(abs(actual-expected))))
        # CPU candidate only: oriented solid angle sum, with abs to permit a
        # globally reversed closed surface. Open/nonmanifold input still needs
        # validation and an explicit unsupported-input policy before adoption.
        angle = np.zeros(x.shape)
        for triangle in fixture['triangles']:
            # Move the triangle axis explicitly; points have three spatial axes.
            vectors = np.array(triangle)[None, None, None, :, :]-points[..., None, :]
            a, b, cc = vectors[..., 0, :], vectors[..., 1, :], vectors[..., 2, :]
            if np.linalg.norm(np.cross(np.array(triangle)[1]-triangle[0], np.array(triangle)[2]-triangle[0])) == 0:
                continue
            la, lb, lc = [np.linalg.norm(v, axis=-1) for v in [a, b, cc]]
            numerator = np.sum(a*np.cross(b, cc), axis=-1)
            denominator = la*lb*lc+np.sum(a*b, axis=-1)*lc+np.sum(b*cc, axis=-1)*la+np.sum(cc*a, axis=-1)*lb
            angle += 2*np.arctan2(numerator, denominator)
        candidate_inside = abs(angle) > 2*np.pi
        row['winding_candidate_sign_errors'] = int(((candidate_inside != inside) & mask).sum())
        if name in plan['supported']:
            row['passed'] = row['nonfinite'] == 0 and row['sign_errors'] == 0 and row['max_distance_error'] <= plan['distance_abs']
        report['results'].append(row)
    for name in ['asset','fallback']:
        file=dest/(name+'.bin')
        row=dict(name=name,passed=file.exists())
        if file.exists():
            raw=np.fromfile(file,np.float32); row['passed']=bool(np.isfinite(raw).all())
            if name=='fallback':
                lo,hi=raw[:3],raw[3:6]
                z,y,x=np.meshgrid(*[np.linspace(lo[i],hi[i],17) for i in [2,1,0]],indexing='ij')
                exact=np.sqrt(x*x+y*y+z*z)-.5
                row['sphere_distance_error']=float(np.max(abs(raw[6:].reshape(exact.shape)-exact)))
                row['passed'] &= row['sphere_distance_error']<=plan['fallback_sphere_abs']
        report['results'].append(row)
    report['passed'] = all(x.get('passed', True) for x in report['results'])
    (dest/'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
