"""Spot light cookies for the default pipeline.

The default pipeline reads cookies from one image, "lightCookies": a 4 x 4
grid of square tiles, numbered 1 to 16 left to right, top to bottom. A spot
light shines through tile N when its image slot is N:

    import numpy as np
    from shoonyakasha import cookies

    window = np.zeros((256, 256, 3), np.uint8)
    window[32:224:64, :] = 255                       # four bright bars
    engine.set_pipeline_image_data("lightCookies", cookies.atlas([window]))
    engine.scene.set_light_source_image(spot, 1)

A tile fills the square around the spot's cone, its top towards the light's
up axis. An atlas from a file works the same way:
engine.set_pipeline_image("lightCookies", "cookies.png").
"""

TILES_ACROSS = 4


def atlas(tiles, tile_size=256):
    """The lightCookies image for up to 16 tiles, as a (4 * tile_size,
    4 * tile_size, 4) uint8 NumPy array of sRGB texels for
    engine.set_pipeline_image_data.

    Each tile is an array shaped (height, width), (height, width, 3) or
    (height, width, 4), uint8, or float from 0 to 1, which is scaled to 0 to
    255 as it is. It is resampled to tile_size x tile_size, nearest texel.
    Tiles not given are white.
    """
    import numpy as np

    tiles = list(tiles)
    if len(tiles) > TILES_ACROSS * TILES_ACROSS:
        raise ValueError("at most %d cookie tiles" % (TILES_ACROSS * TILES_ACROSS))
    size = TILES_ACROSS * tile_size
    out = np.full((size, size, 4), 255, np.uint8)
    for n, tile in enumerate(tiles):
        texels = np.asarray(tile)
        if texels.dtype != np.uint8:
            texels = (np.clip(texels, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
        if texels.ndim == 2:
            texels = texels[:, :, None].repeat(3, axis=2)
        if texels.ndim != 3 or texels.shape[2] not in (3, 4):
            raise ValueError("tile %d: give (height, width), (height, width, 3) or (height, width, 4)" % n)
        if texels.shape[2] == 3:
            texels = np.concatenate([texels, np.full(texels.shape[:2] + (1,), 255, np.uint8)], axis=2)
        rows = np.arange(tile_size) * texels.shape[0] // tile_size
        columns = np.arange(tile_size) * texels.shape[1] // tile_size
        y, x = divmod(n, TILES_ACROSS)
        out[y * tile_size:(y + 1) * tile_size, x * tile_size:(x + 1) * tile_size] = texels[rows][:, columns]
    return out
