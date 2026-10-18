"""What run.py captures from each Python example.

A shot: folder and script under examples/python/, args for the example, and
when to capture: `frames` after the first frame, or else `seconds` (default 3).
`keys` presses keys on given frames, such as 'F2@30' to hide a panel.
`timeout` bounds the whole run, loading included (default 180 s). Social and
clip shots add a `name` and the `assets` whose credits they need; clips add
`clip` seconds, `vertical` for 1080x1920, and `gif` = (start, length) seconds.
"""

PYTHON = [
    dict(folder='getting_started/demo', script='demo.py'),
    dict(folder='getting_started/ecs_bindings_demo', script='ecs_bindings_demo.py'),
    dict(folder='getting_started/ui_canvas_demo', script='ui_canvas_demo.py'),
    dict(folder='animation/skinned_fox_demo', script='skinned_fox_demo.py'),
    dict(folder='games_2d/sprite_ui_test', script='sprite_ui_demo.py'),
    dict(folder='games_2d/full_showcase', script='showcase_demo.py'),
    dict(folder='games_2d/dakini_temple', script='temple.py'),
    dict(folder='rendering/japanese_shrine', script='shrine.py'),
    dict(folder='rendering/sponza', script='sponza.py', seconds=5),
    dict(folder='rendering/alley', script='alley.py', seconds=8, timeout=300),
    dict(folder='rendering/bistro', script='bistro.py', seconds=8, timeout=600),
    dict(folder='rendering/scene_lights', script='scene_lights.py', seconds=5),
    dict(folder='rendering/volume_tiled', script='volume_tiled.py', seconds=5),
    dict(folder='rendering/showroom', script='showroom.py', args=['--models', 'corvette_c8', '--width', '1280', '--height', '720'],
         seconds=20, timeout=600),
    dict(folder='rendering/museum', script='museum.py', args=['--width', '1280', '--height', '720'],
         seconds=20, timeout=600),
    dict(folder='rendering/reference_pt', script='reference_pt.py', frames=1024, timeout=600),
    dict(folder='rendering/irradiance_cache', script='irradiance_cache.py', args=['--scene', 'conference'], frames=300, timeout=300),
    dict(folder='rendering/restir_gi', script='restir_gi.py', args=['--scene', 'viziers'], frames=300, timeout=300),
    dict(folder='rendering/rt_reflections', script='rt_reflections.py', args=['--scene', 'gas_stations'], frames=300, timeout=300),
    dict(folder='rendering/wave_optics', script='wave_optics.py', frames=1024, timeout=600),
]

KAJIYA = ['viziers', 'gas_stations', 'battle', 'warhammer', 'conference']
CARS = ['corvette_c8', 'porsche_911_turbo', 'eta2_interceptor']
SHOWROOM_CARS = ['gaz13_chaika', 'shelby_cobra', 'alfa_33_stradale_1968', 'chevrolet_camaro', 'alfa_montreal',
                 'porsche_911_turbo', 'alfa_gtv6', 'corvette_c8', 'lamborghini_revuelto', 'alfa_33_stradale_2024']
PANEL_OFF = 'F2@30'


def path_tracer(name, script, args, assets, frames=4096):
    return dict(name=name, folder='rendering/' + script, script=script + '.py', args=args,
                frames=frames, keys=PANEL_OFF, timeout=1200, assets=assets)


SOCIAL = [
    path_tracer('pt_sponza', 'reference_pt', [], ['sponza']),
    *[path_tracer('pt_' + scene, 'reference_pt', ['--scene', scene], [scene]) for scene in KAJIYA],
    path_tracer('restir_gi_battle', 'restir_gi', ['--scene', 'battle'], ['battle'], frames=600),
    path_tracer('rt_reflections_warhammer', 'rt_reflections', ['--scene', 'warhammer'], ['warhammer'], frames=600),
    path_tracer('irradiance_cache_conference', 'irradiance_cache', ['--scene', 'conference'], ['conference'],
                frames=600),
    path_tracer('wave_optics', 'wave_optics', [], []),
    path_tracer('wave_optics_sodium', 'wave_optics', ['--lamp', 'sodium'], []),
    path_tracer('wave_optics_mercury_disc', 'wave_optics', ['--lamp', 'mercury', '--lamp-radius', '0.005'], []),
    path_tracer('wave_optics_abalone', 'wave_optics', ['--scene', 'abalone'], ['abalone']),
    path_tracer('wave_optics_dish', 'wave_optics', ['--scene', 'dish'], ['dish']),
    path_tracer('wave_optics_lamp', 'wave_optics', ['--scene', 'lamp'], ['lamp']),
    path_tracer('wave_optics_metal_spheres', 'wave_optics', ['--scene', 'metal-spheres'], []),
    path_tracer('wave_optics_suzanne', 'wave_optics', ['--scene', 'suzanne'], []),
    dict(name='bistro_night', folder='rendering/bistro', script='bistro.py', seconds=16, timeout=900,
         assets=['bistro']),
    dict(name='alley_night', folder='rendering/alley', script='alley.py', keys='N@30', seconds=20, timeout=600,
         assets=['polyhaven']),
    dict(name='shrine', folder='rendering/japanese_shrine', script='shrine.py', seconds=6, assets=['shrine']),
    dict(name='volume_tiled_262k', folder='rendering/volume_tiled', script='volume_tiled.py',
         args=['--lights', '262144'], keys=PANEL_OFF, seconds=8, timeout=300, assets=['sponza']),
    dict(name='scene_lights', folder='rendering/scene_lights', script='scene_lights.py', seconds=5,
         assets=['sponza']),
    dict(name='showroom', folder='rendering/showroom', script='showroom.py', own=True,
         args=['--screenshots', '{out}', '--models', ','.join(CARS)], timeout=3600,
         assets=['showroom:' + car for car in CARS]),
    dict(name='museum', folder='rendering/museum', script='museum.py', own=True,
         args=['--screenshots', '{out}', '--models', 'gaz13_chaika'], timeout=1800,
         assets=['showroom:gaz13_chaika']),
]


def clip(name, folder, script, length, args=(), assets=(), vertical=False, gif=None, **more):
    return dict(name=name + ('_vertical' if vertical else ''), folder='rendering/' + folder, script=script,
                args=list(args), clip=length, vertical=vertical, gif=None if vertical else gif,
                assets=list(assets), timeout=more.pop('timeout', 900), **more)


CLIPS = [
    clip('bistro_walk', 'bistro', 'bistro.py', 20, assets=['bistro'], seconds=4, gif=(6, 4)),
    clip('bistro_walk', 'bistro', 'bistro.py', 15, assets=['bistro'], seconds=4, vertical=True),
    clip('alley_nightfall', 'alley', 'alley.py', 16, assets=['polyhaven'], frames=60, keys='N@90', gif=(4, 5)),
    clip('shrine_orbit', 'japanese_shrine', 'shrine.py', 15, assets=['shrine'], gif=(0, 4)),
    clip('shrine_orbit', 'japanese_shrine', 'shrine.py', 15, assets=['shrine'], vertical=True),
    clip('restir_gi_walk', 'restir_gi', 'restir_gi.py', 12, ['--scene', 'viziers', '--walk', '0.4'], ['viziers'],
         frames=120, keys=PANEL_OFF, gif=(2, 4)),
    clip('restir_gi_walk', 'restir_gi', 'restir_gi.py', 12, ['--scene', 'viziers', '--walk', '0.4'], ['viziers'],
         frames=120, keys=PANEL_OFF, vertical=True),
    clip('rt_reflections_walk', 'rt_reflections', 'rt_reflections.py', 12, ['--walk', '1.0'], ['sponza'],
         frames=120, keys=PANEL_OFF, gif=(2, 4)),
    clip('pt_converging', 'reference_pt', 'reference_pt.py', 5, ['--scene', 'viziers'], ['viziers'],
         frames=60, keys='F2@30,R@59', gif=(0, 3)),
    clip('wave_optics_spectra', 'wave_optics', 'wave_optics.py', 12, frames=60,
         keys='F2@30,L@120,L@180,L@240,L@300', gif=(0, 12)),
    clip('volume_tiled_262k', 'volume_tiled', 'volume_tiled.py', 10, ['--lights', '262144'], ['sponza'],
         keys=PANEL_OFF, seconds=4, gif=(0, 4)),
    clip('showroom_tour', 'showroom', 'showroom.py', 0, ['--record', '{out}', '--models', 'corvette_c8',
         '--seconds', '20'], ['showroom:corvette_c8'], own=True, timeout=3600),
    *[clip('showroom_' + car, 'showroom', 'showroom.py', 0, ['--record', '{out}', '--models', car,
           '--seconds', '20', '--vertical'], ['showroom:' + car], own=True, vertical=True, timeout=3600)
      for car in SHOWROOM_CARS],
    clip('museum_tour', 'museum', 'museum.py', 0, ['--record', '{out}', '--models', 'gaz13_chaika,corvette_c8',
         '--seconds', '12'], ['showroom:gaz13_chaika', 'showroom:corvette_c8'], own=True, timeout=3600),
]

CREDITS = {
    'sponza': 'Sponza by Crytek (glTF-Sample-Assets), under the CRYENGINE Limited License Agreement '
              '(https://www.cryengine.com/ce-terms); check its terms before publishing.',
    'abalone': '"Iridescence Abalone" by Abby Crawford and Eric Chadwick (glTF-Sample-Assets), CC BY 4.0.',
    'lamp': '"Iridescence Lamp" by Eric Chadwick (glTF-Sample-Assets), CC BY 4.0.',
    'dish': '"Iridescent Dish with Olives" by Eric Chadwick (glTF-Sample-Assets), CC BY 4.0.',
    'shrine': '"Japanese Shrine - Traditional Temple" (https://skfb.ly/pMEO8) by aumiella, CC BY 4.0.',
    'bistro': 'Amazon Lumberyard Bistro, Open Research Content Archive (ORCA), CC BY 4.0.',
    'polyhaven': None,
    'viziers': '"Vizier\'s observation deck" by winion, CC BY 4.0.',
    'gas_stations': '"gas stations fixed" (https://skfb.ly/6RGDX) by John_Constantine, CC BY 4.0.',
    'battle': '"Flying world - Battle of the Trash god" (https://skfb.ly/6SMIE) by burunduk, CC BY 4.0; '
              'materials modified by Embark Studios.',
    'warhammer': '"Warhammer scene." by sheffrator, CC BY 4.0.',
    'conference': 'Conference room, (c) Anat Grynberg and Greg Ward, via https://casual-effects.com/data/.',
}
