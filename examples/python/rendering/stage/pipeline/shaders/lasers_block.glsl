// lasers_block.glsl - the Lasers block's members, for shaders to declare it
// with before including lasers.glsl.

#ifndef STAGE_LASERS_BLOCK_GLSL
#define STAGE_LASERS_BLOCK_GLSL

#define LASER_PROJECTORS 4
#define LASER_BEAMS 16

// scene.custom.laser.* (pipeline.json "Lasers"); see README.md.
#define LASERS_BLOCK                                                               \
    vec4 origin[LASER_PROJECTORS]; /* xyz the aperture, w the pattern */           \
    vec4 axis[LASER_PROJECTORS];   /* xyz the direction it points, w its beams */  \
    vec4 up[LASER_PROJECTORS];     /* xyz its up, w the spread: fan half-width or cone half-angle (radians) */ \
    vec4 color[LASER_PROJECTORS];  /* rgb colour times power (watts), w phase */   \
    vec4 params;  /* x seconds of show, y beam radius (m), z dot brightness, w longest beam (m) */

#endif
