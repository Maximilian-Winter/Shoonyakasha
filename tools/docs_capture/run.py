"""Run example captures from their source working directories. See the image README.

    run.py cpp | python          documentation stills into docs/images/examples/
    run.py social                1920x1080 stills into build/docs-capture/social/stills/
    run.py clips                 clips and GIF loops into build/docs-capture/social/
    run.py gifs                  the GIF loops again, from the clips already recorded
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from shots import CLIPS, CREDITS, PYTHON, SOCIAL

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/docs-capture'
SOCIAL_DIR = WORK / 'social'
GIF_WIDTH = 480
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


def base_env():
    env = os.environ.copy()
    for key in [key for key in env if key.startswith('DOCS_CAPTURE_')]:
        env.pop(key)
    env.update(SHOONYAKASHA_ASSET_DIR=str(ROOT / 'assets'), PYTHONIOENCODING='utf-8')
    return env


def run_cpp(folder, target, build, temporary, log_path):
    env = base_env()
    env.update(DOCS_CAPTURE_PATH=str(temporary),
               DOCS_CAPTURE_SECONDS='1' if folder.startswith('physics/') else '3')
    if folder.startswith('physics/'):
        env['DOCS_CAPTURE_PLAY'] = '1'
    if Path(folder).name == 'facade_test':
        env['DOCS_CAPTURE_BOX'] = '1'
    binary = build.resolve() / 'examples/cpp' / folder / (target + ('.exe' if os.name == 'nt' else ''))
    if not binary.exists():
        binary = binary.parent / 'Release' / binary.name
    with log_path.open('w', encoding='utf-8') as log:
        process = subprocess.Popen([str(binary)], cwd=ROOT / 'examples/cpp' / folder,
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
    return process.returncode == 0 and temporary.exists()


def run_python(interpreter, shot, log_path, **capture):
    """Run one shot through python_capture.py. capture: path or record, size,
    clip seconds. Returns True once the wrapper reports success. A shot with
    `own` captures itself: '{out}' in its args becomes the output path, and it
    succeeds when the example exits cleanly having written it."""
    env = base_env()
    env['DOCS_CAPTURE_SIZE'] = capture.get('size', '1280x720')
    if 'frames' in shot:
        env['DOCS_CAPTURE_FRAMES'] = str(shot['frames'])
    env['DOCS_CAPTURE_SECONDS'] = str(shot.get('seconds', 3))
    if 'keys' in shot:
        env['DOCS_CAPTURE_KEYS'] = shot['keys']
    out = capture.get('record') or capture.get('path')
    if shot.get('own'):
        env['DOCS_CAPTURE_PASSIVE'] = '1'
    elif 'record' in capture:
        env['DOCS_CAPTURE_RECORD'] = str(out)
        env['DOCS_CAPTURE_CLIP_SECONDS'] = str(shot['clip'])
    else:
        env['DOCS_CAPTURE_PATH'] = str(out)
    args = [arg.replace('{out}', str(out)) for arg in shot.get('args', ())]
    command = [interpreter, str(Path(__file__).resolve().with_name('python_capture.py')), shot['script']] + args
    with log_path.open('w', encoding='utf-8') as log:
        process = subprocess.Popen(command, cwd=ROOT / 'examples/python' / shot['folder'],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            process.wait(timeout=shot.get('timeout', 180))
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=10)
    if shot.get('own'):
        return process.returncode == 0 and Path(out).exists()
    return 'DOCS_CAPTURE_RESULT True' in log_path.read_text(encoding='utf-8', errors='replace')


def make_gif(clip, gif, start, length, width):
    """A looping GIF from part of a clip, with a palette made for it."""
    filters = f'fps=12,scale={width}:-2:flags=lanczos,split[a][b];[a]palettegen=stats_mode=diff[p];' \
              f'[b][p]paletteuse=dither=sierra2_4a'
    result = subprocess.run(['ffmpeg', '-y', '-v', 'error', '-ss', str(start), '-t', str(length),
                             '-i', str(clip), '-filter_complex', filters, '-loop', '0', str(gif)])
    return result.returncode == 0


def credit(asset):
    """An asset's credit line: from CREDITS, or `showroom:NAME` from that model's showroom.json."""
    if asset.startswith('showroom:'):
        path = ROOT / 'assets/showroom' / asset.split(':', 1)[1] / 'showroom.json'
        return json.loads(path.read_text(encoding='utf-8', errors='replace')).get('credit') if path.exists() else None
    return CREDITS.get(asset)


def write_credits(names):
    """CREDITS.md beside the social captures: the attribution each file needs."""
    lines = ['# Credits for these captures', '',
             'Rendered with Shoonyakasha. Each file below shows assets that ask for this credit; '
             'a GIF has its clip\'s credit:', '']
    for name, assets in sorted(names.items()):
        needed = [line for line in map(credit, assets) if line]
        lines.append(f'- **{name}**: ' + (' '.join(needed) if needed else 'no attribution required.'))
    (SOCIAL_DIR / 'CREDITS.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=['cpp', 'python', 'social', 'clips', 'gifs'])
    parser.add_argument('--build', type=Path, default=WORK / 'native')
    parser.add_argument('--python', default=sys.executable, help='Python with native bindings installed')
    parser.add_argument('--only', help='Example directory basename (cpp, python) or shot name (social, clips)')
    args = parser.parse_args()
    interpreter = shutil.which(args.python) or str(Path(args.python).resolve())
    WORK.mkdir(parents=True, exist_ok=True)
    failed = []

    if args.mode == 'cpp':
        items = [item for item in CPP if not args.only or Path(item[0]).name == args.only]
        for folder, target in items:
            name = Path(folder).name
            temporary = WORK / f'cpp-{name}.png'
            temporary.unlink(missing_ok=True)
            if run_cpp(folder, target, args.build, temporary, WORK / f'cpp-{name}.log'):
                destination = ROOT / 'docs/images/examples/cpp' / f'{name}.png'
                shutil.copyfile(temporary, destination)
                print(f'{name}: captured', flush=True)
            else:
                failed.append(name)
                print(f'{name}: FAILED; see {WORK / f"cpp-{name}.log"}', flush=True)
        return bool(failed)

    if args.mode == 'gifs':
        (SOCIAL_DIR / 'gifs').mkdir(parents=True, exist_ok=True)
        for shot in CLIPS:
            clip = SOCIAL_DIR / 'clips' / f'{shot["name"]}.mp4'
            if shot.get('gif') and clip.exists() and (not args.only or shot['name'] == args.only):
                start, length = shot['gif']
                ok = make_gif(clip, SOCIAL_DIR / 'gifs' / f'{shot["name"]}.gif', start, length, GIF_WIDTH)
                print(f'{shot["name"]}.gif: {"made" if ok else "FAILED"}', flush=True)
                if not ok:
                    failed.append(shot['name'])
        return bool(failed)

    if args.mode == 'python':
        shots = [s for s in PYTHON if not args.only or Path(s['folder']).name == args.only]
    else:
        shots = [s for s in (SOCIAL if args.mode == 'social' else CLIPS) if not args.only or s['name'] == args.only]
    if not shots:
        parser.error('Nothing to capture (Pong is excluded pending artwork permission)')

    for shot in shots:
        name = shot.get('name', Path(shot['folder']).name)
        log_path = WORK / f'{args.mode}-{name}.log'
        if args.mode == 'clips':
            vertical = shot.get('vertical', False)
            folder = SOCIAL_DIR / ('vertical' if vertical else 'clips')
            folder.mkdir(parents=True, exist_ok=True)
            clip = folder / f'{name}.mp4'
            clip.unlink(missing_ok=True)
            ok = run_python(interpreter, shot, log_path, record=clip,
                            size='1080x1920' if vertical else '1920x1080') and clip.exists()
            if ok and shot.get('gif'):
                (SOCIAL_DIR / 'gifs').mkdir(parents=True, exist_ok=True)
                start, length = shot['gif']
                ok = make_gif(clip, SOCIAL_DIR / 'gifs' / f'{name}.gif', start, length, GIF_WIDTH)
        elif shot.get('own'):
            folder = SOCIAL_DIR / 'stills' / name
            shutil.rmtree(folder, ignore_errors=True)
            ok = run_python(interpreter, shot, log_path, path=folder, size='1920x1080')
        else:
            temporary = WORK / f'{args.mode}-{name}.png'
            temporary.unlink(missing_ok=True)
            size = '1920x1080' if args.mode == 'social' else '1280x720'
            ok = run_python(interpreter, shot, log_path, path=temporary, size=size) and temporary.exists()
            if ok:
                destination = (SOCIAL_DIR / 'stills' / f'{name}.png' if args.mode == 'social'
                               else ROOT / 'docs/images/examples/python' / f'{name}.png')
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(temporary, destination)
        print(f'{name}: {"captured" if ok else f"FAILED; see {log_path}"}', flush=True)
        if not ok:
            failed.append(name)

    if args.mode in ('social', 'clips'):
        captured = {}
        for shot in SOCIAL + CLIPS:
            sub = 'stills' if shot in SOCIAL else ('vertical' if shot.get('vertical') else 'clips')
            suffix = '/' if shot.get('own') and sub == 'stills' else ('.png' if sub == 'stills' else '.mp4')
            path = SOCIAL_DIR / sub / (shot['name'] + suffix.strip('/'))
            if path.exists():
                captured[f'{sub}/{shot["name"]}{suffix}'] = shot.get('assets', ())
        write_credits(captured)
    return bool(failed)


if __name__ == '__main__':
    raise SystemExit(main())
