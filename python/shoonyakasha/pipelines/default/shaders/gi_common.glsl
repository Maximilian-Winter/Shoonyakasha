// gi_common.glsl - what the irradiance cache's compute passes share: the
// cache's buffers at set 0 ("ircacheSet"), the camera at set 1 ("cameraSet")
// and the settings with the suns' cascades at set 2 ("settingsSet").

#ifndef DEFAULT_GI_COMMON_GLSL
#define DEFAULT_GI_COMMON_GLSL

#include "common.glsl"

layout(set = 1, binding = 0) uniform Camera { DEFAULT_CAMERA_BLOCK } camera;
layout(set = 2, binding = 0) uniform Settings { DEFAULT_SETTINGS_BLOCK } settings;
layout(set = 2, binding = 1) uniform Cascades { DEFAULT_CASCADES_BLOCK } cascades;

#define IRCACHE_SET 0
#include "ircache.glsl"

#endif
