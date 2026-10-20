// lasers_block.glsl - the Lasers block's members, for shaders to declare it
// with before including lasers.glsl.

#ifndef STAGE_LASERS_BLOCK_GLSL
#define STAGE_LASERS_BLOCK_GLSL

#define LASER_PROJECTORS 4
#define LASER_BEAMS 16

// scene.custom.laser.* (pipeline.json "Lasers"); see README.md.
#define LASERS_BLOCK                                                               \
    vec4 origin0; /* xyz the aperture, w the pattern */                            \
    vec4 axis0;   /* xyz the direction it points, w its beams */                   \
    vec4 up0;     /* xyz its up, w the spread: fan half-width or cone half-angle (radians) */ \
    vec4 color0;  /* rgb colour times power (watts), w phase */                    \
    vec4 origin1; vec4 axis1; vec4 up1; vec4 color1;                               \
    vec4 origin2; vec4 axis2; vec4 up2; vec4 color2;                               \
    vec4 origin3; vec4 axis3; vec4 up3; vec4 color3;                               \
    vec4 params;  /* x seconds of show, y beam radius (m), z dot brightness, w longest beam (m) */

#endif
