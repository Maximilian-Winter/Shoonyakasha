"""Run example captures from their source working directories. See the image README."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/docs-capture'
CPP = [
    ('api/facade_test', 'FacadeTest'),
    ('api/instancing_test', 'InstancingTest'),
    ('rendering/declarative_sponza_test', 'DeclarativeSponzaTest'),
    ('rendering/bloom_test', 'ShoonyakashaBloomTest'),
    ('compute/particle_test', 'ShoonyakashaParticleTest'),
    ('compute/particle_flow_example', 'ParticleFlowExample'),
    ('compute/ssbo_data_flow_example', 'SSBODataFlowExample'),
    ('physics/physics_test', 'PhysicsTest'),
    ('physics/pbr_physics_particles', 'PbrPhysicsParticles'),
    ('animation/skinned_mesh_test', 'SkinnedMeshTest'),
]
PYTHON = [
    ('getting_started/demo', 'demo.py'),
    ('getting_started/ecs_bindings_demo', 'ecs_bindings_demo.py'),
    ('animation/skinned_fox_demo', 'skinned_fox_demo.py'),
    ('games_2d/sprite_ui_test', 'sprite_ui_demo.py'),
    ('games_2d/full_showcase', 'showcase_demo.py'),
    ('games_2d/dakini_temple', 'temple.py'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('language', choices=['cpp', 'python'])
    parser.add_argument('--build', type=Path, default=WORK / 'native')
    parser.add_argument('--python', default=sys.executable, help='Python with native bindings installed')
    parser.add_argument('--only', help='Example directory basename')
    args = parser.parse_args()
    items = CPP if args.language == 'cpp' else PYTHON
    if args.only:
        items = [item for item in items if Path(item[0]).name == args.only]
        if not items:
            parser.error('Unknown example (Pong is excluded pending artwork permission)')
    WORK.mkdir(parents=True, exist_ok=True)
    failed = []
    for folder, target in items:
        name = Path(folder).name
        temporary = WORK / f'{args.language}-{name}.png'
        temporary.unlink(missing_ok=True)
        destination = ROOT / 'docs/images/examples' / args.language / f'{name}.png'
        log_path = WORK / f'{args.language}-{name}.log'
        env = os.environ.copy()
        for key in ('DOCS_CAPTURE_BOX', 'DOCS_CAPTURE_PLAY'):
            env.pop(key, None)
        env.update(SHOONYAKASHA_ASSET_DIR=str(ROOT / 'assets'),
                   DOCS_CAPTURE_PATH=str(temporary), PYTHONIOENCODING='utf-8',
                   DOCS_CAPTURE_SECONDS='1' if folder.startswith('physics/') else '3')
        if folder.startswith('physics/'):
            env['DOCS_CAPTURE_PLAY'] = '1'
        if name == 'facade_test':
            env['DOCS_CAPTURE_BOX'] = '1'
        if args.language == 'cpp':
            binary = args.build.resolve() / 'examples/cpp' / folder / (target + ('.exe' if os.name == 'nt' else ''))
            if not binary.exists():
                binary = binary.parent / 'Release' / binary.name
            command = [str(binary)]
        else:
            interpreter = shutil.which(args.python) or str(Path(args.python).resolve())
            command = [interpreter, str(Path(__file__).resolve().with_name('python_capture.py')), target]
        success = False
        with log_path.open('w', encoding='utf-8') as log:
            process = subprocess.Popen(command, cwd=ROOT / 'examples' / args.language / folder,
                                       env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 90
                while process.poll() is None and time.monotonic() < deadline:
                    if args.language == 'python' and 'DOCS_CAPTURE_RESULT True' in log_path.read_text(encoding='utf-8', errors='replace'):
                        success = True
                        break
                    time.sleep(0.25)
                if args.language == 'cpp':
                    success = process.poll() == 0 and temporary.exists()
            finally:
                # Python has no public quit method. Stop only this child after capture.
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
        if success and temporary.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(temporary, destination)
            print(f'{name}: captured', flush=True)
        else:
            failed.append(name)
            print(f'{name}: FAILED; see {log_path}', flush=True)
    return bool(failed)


if __name__ == '__main__':
    raise SystemExit(main())
