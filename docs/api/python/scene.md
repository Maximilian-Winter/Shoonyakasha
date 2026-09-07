# Scene

Access: `engine.scene`. [Other language reference](../cpp/scene-api.md).

[Binding implementation](../../../python/shoonyakasha/_shoonyakasha.pyx). Import the package as `import shoonyakasha as sk`.

Provides entity lifecycle and typed access to built-in components. Obtain it from a running engine in init or later. Entity handles are unsigned 32-bit values, represented as Python integers; `NullEntity` / `NULL_ENTITY` is `4294967295`. Check validity after destruction or scene replacement; handles are not persistent IDs.

## Entity and component contract

Creating an entity adds Transform and Active. Name/tag lookup, parent/child access, local transforms, world transforms, camera/light properties, material parameters, visibility, sorting, and animation are exposed below. Python vector getters return tuples; world matrices are column-major tuples of four tuples. C++ uses GLM values.

Positions and scales are local to the parent; transform rotation setters use Euler radians. Camera FOV setters use degrees. World transforms are refreshed by the transform system: a local setter does not imply all cached world values have updated immediately.

Name-based component access supports only the registered native names returned by `getComponentNames()` / `get_component_names()`. It does not expose arbitrary component fields. User-defined Python payloads belong to [Ecs](../../guides/script-ecs.md).

Missing components are handled with method-specific defaults/no-ops in the facade; do not use those fallback values to infer component presence. Check entity validity and component presence first. Material getters accept explicit defaults. Texture setters return false when loading or wiring fails.

## Rendering and animation

Entity layer masks are eight bits (0–255), default 255. A pass renders an entity if its mask intersects the pass mask. Sort keys order lower values first for `sortMode: sort_key`.

Text labels generate separate glyph entities. Use text-specific setters for layer, sort order, and visibility. Hide a label before destroying it: destroying the label alone can leave glyphs visible. See [sprites/UI/text](../../guides/sprites-ui-text.md).

Clip indices are zero-based; duration/time are seconds. Play resets time; stop resets to zero. Clip queries return zero/empty values on nonanimated entities and current clip is -1 when none is selected. Use a skinned pipeline to render animation.

## Serialization

Save/load delegate to the partial ECS snapshot format. Load clears existing entities and remaps saved IDs; it does not merge with loaded glTF entities. Save does not check stream failure, so true is not proof of a successful disk write. See [serialization limitations](../../guides/scene-serialization.md) before using this for persistence.

<!-- BEGIN SOURCE API -->

## Members

Signatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.

| Member | Returns / property value | Description |
|---|---|---|
| `create_entity(name="")` | int | Create a new entity with optional name. |
| `destroy_entity(entity)` | None | Destroy an entity. |
| `is_valid(entity)` | bool | Check if an entity handle is valid. |
| `entity_count (read-only property)` | int | Total entity count. |
| `find_entity_by_name(name)` | int | Find entity by name (returns NULL_ENTITY if not found). |
| `find_entities_with_tag(tag)` | list[int] | Find all entities with a given tag. |
| `get_main_camera()` | int | Get the main camera entity. |
| `get_all_entities()` | list[int] | Get all entity handles. |
| `add_component(entity, component_name)` | bool | Add a component by name (e.g. 'Transform', 'Light', 'Camera'). |
| `remove_component(entity, component_name)` | bool | Remove a component by name. |
| `has_component(entity, component_name)` | bool | Check if entity has a component by name. |
| `get_component_names()` | list[str] | List all registered component type names. |
| `get_name(entity)` | str | Calls native `getName`; see the class contract above. |
| `set_name(entity, name)` | None | Calls native `setName`; see the class contract above. |
| `get_tag(entity)` | str | Calls native `getTag`; see the class contract above. |
| `set_tag(entity, tag)` | None | Calls native `setTag`; see the class contract above. |
| `is_active(entity)` | bool | Calls native `isActive`; see the class contract above. |
| `set_active(entity, active)` | None | Calls native `setActive`; see the class contract above. |
| `get_position(entity)` | 3-tuple | Get entity position as (x, y, z) tuple. |
| `set_position(entity, pos)` | None | Set entity position from (x, y, z) tuple. |
| `get_rotation(entity)` | 3-tuple | Get entity rotation (euler radians) as (x, y, z) tuple. |
| `set_rotation(entity, rot)` | None | Set entity rotation (euler radians) from (x, y, z) tuple. |
| `get_scale(entity)` | 3-tuple | Get entity scale as (x, y, z) tuple. |
| `set_scale(entity, scale)` | None | Set entity scale from (x, y, z) tuple. |
| `get_world_position(entity)` | 3-tuple | Get world-space position as (x, y, z) tuple. |
| `get_world_matrix(entity)` | 4×4 tuple (column-major) | Get world matrix as 4x4 tuple-of-tuples (column-major). |
| `get_forward(entity)` | 3-tuple | Get forward direction as (x, y, z) tuple. |
| `get_right(entity)` | 3-tuple | Get right direction as (x, y, z) tuple. |
| `get_up(entity)` | 3-tuple | Get up direction as (x, y, z) tuple. |
| `get_camera_type(entity)` | int | Calls native `getCameraType`; see the class contract above. |
| `set_camera_type(entity, camera_type)` | None | Calls native `setCameraType`; see the class contract above. |
| `get_camera_fov(entity)` | float | Calls native `getCameraFov`; see the class contract above. |
| `set_camera_fov(entity, fov)` | None | Calls native `setCameraFov`; see the class contract above. |
| `get_camera_near(entity)` | float | Calls native `getCameraNear`; see the class contract above. |
| `set_camera_near(entity, near_plane)` | None | Calls native `setCameraNear`; see the class contract above. |
| `get_camera_far(entity)` | float | Calls native `getCameraFar`; see the class contract above. |
| `set_camera_far(entity, far_plane)` | None | Calls native `setCameraFar`; see the class contract above. |
| `get_camera_ortho_size(entity)` | float | Calls native `getCameraOrthoSize`; see the class contract above. |
| `set_camera_ortho_size(entity, size)` | None | Calls native `setCameraOrthoSize`; see the class contract above. |
| `is_camera_main(entity)` | bool | Calls native `isCameraMain`; see the class contract above. |
| `set_camera_main(entity, is_main)` | None | Calls native `setCameraMain`; see the class contract above. |
| `get_light_type(entity)` | int | Calls native `getLightType`; see the class contract above. |
| `set_light_type(entity, light_type)` | None | Calls native `setLightType`; see the class contract above. |
| `get_light_color(entity)` | 3-tuple | Calls native `getLightColor`; see the class contract above. |
| `set_light_color(entity, color)` | None | Calls native `setLightColor`; see the class contract above. |
| `get_light_intensity(entity)` | float | Calls native `getLightIntensity`; see the class contract above. |
| `set_light_intensity(entity, intensity)` | None | Calls native `setLightIntensity`; see the class contract above. |
| `get_light_range(entity)` | float | Calls native `getLightRange`; see the class contract above. |
| `set_light_range(entity, range)` | None | Calls native `setLightRange`; see the class contract above. |
| `get_light_cast_shadows(entity)` | bool | Calls native `getLightCastShadows`; see the class contract above. |
| `set_light_cast_shadows(entity, cast_shadows)` | None | Calls native `setLightCastShadows`; see the class contract above. |
| `set_material_float(entity, param, value)` | None | Calls native `setMaterialFloat`; see the class contract above. |
| `get_material_float(entity, param, default_val=0.0)` | float | Calls native `getMaterialFloat`; see the class contract above. |
| `set_material_vec3(entity, param, value)` | None | Calls native `setMaterialVec3`; see the class contract above. |
| `get_material_vec3(entity, param, default_val=(0.0, 0.0, 0.0))` | 3-tuple | Calls native `getMaterialVec3`; see the class contract above. |
| `set_material_vec4(entity, param, value)` | None | Calls native `setMaterialVec4`; see the class contract above. |
| `get_material_vec4(entity, param, default_val=(0.0, 0.0, 0.0, 0.0))` | 4-tuple | Calls native `getMaterialVec4`; see the class contract above. |
| `has_material_param(entity, param)` | bool | Calls native `hasMaterialParam`; see the class contract above. |
| `set_material_texture(entity, slot_name, file_path)` | bool | Calls native `setMaterialTexture`; see the class contract above. |
| `set_sprite_texture(entity, file_path)` | bool | Calls native `setSpriteTexture`; see the class contract above. |
| `set_sprite_color(entity, color)` | None | Calls native `setSpriteColor`; see the class contract above. |
| `get_sprite_color(entity)` | 4-tuple | Calls native `getSpriteColor`; see the class contract above. |
| `set_sprite_uv_rect(entity, uv_rect)` | None | Calls native `setSpriteUVRect`; see the class contract above. |
| `get_sprite_uv_rect(entity)` | 4-tuple | Calls native `getSpriteUVRect`; see the class contract above. |
| `is_screen_space_sprite(entity)` | bool | Calls native `isScreenSpaceSprite`; see the class contract above. |
| `set_ui_anchor(entity, anchor, offset_pixels=(0.0, 0.0))` | None | Calls native `setUIAnchor`; see the class contract above. |
| `get_ui_anchor(entity)` | int | Calls native `getUIAnchor`; see the class contract above. |
| `get_ui_anchor_offset(entity)` | 2-tuple | Calls native `getUIAnchorOffset`; see the class contract above. |
| `set_text(entity, text)` | None | Calls native `setText`; see the class contract above. |
| `get_text(entity)` | str | Calls native `getText`; see the class contract above. |
| `set_text_color(entity, color)` | None | Calls native `setTextColor`; see the class contract above. |
| `set_text_font_size(entity, font_size)` | None | Calls native `setTextFontSize`; see the class contract above. |
| `set_text_align(entity, align)` | None | Calls native `setTextAlign`; see the class contract above. |
| `set_text_layer_mask(entity, mask)` | None | Propagated to every glyph entity generated for this label. |
| `set_text_sort_key(entity, sort_key)` | None | Draw order — lower draws first, so text over a panel needs a higher key than the panel. Labels default to 0. |
| `set_text_visible(entity, visible)` | None | Show or hide a label. This is how to hide text — destroying the label entity leaves its glyph entities on screen. |
| `is_text_visible(entity)` | bool | Calls native `isTextVisible`; see the class contract above. |
| `is_visible(entity)` | bool | Calls native `isVisible`; see the class contract above. |
| `set_visible(entity, visible)` | None | Calls native `setVisible`; see the class contract above. |
| `get_cast_shadows(entity)` | bool | Calls native `getCastShadows`; see the class contract above. |
| `set_cast_shadows(entity, cast_shadows)` | None | Calls native `setCastShadows`; see the class contract above. |
| `get_render_layer_mask(entity)` | int | 8-bit layer mask (bits 0-7, default 0xFF = every layer). |
| `set_render_layer_mask(entity, mask)` | None | Restrict which "renderLayerMask"-filtered passes draw this entity. |
| `get_sort_key(entity)` | int | Calls native `getSortKey`; see the class contract above. |
| `set_sort_key(entity, sort_key)` | None | Draw order hint for passes using "sortMode": "sort_key" - lower draws first. |
| `get_parent(entity)` | int | Calls native `getParent`; see the class contract above. |
| `set_parent(child, parent)` | None | Calls native `setParent`; see the class contract above. |
| `get_children(entity)` | list[int] | Calls native `getChildren`; see the class contract above. |
| `get_animation_clip_count(entity)` | int | Get number of animation clips on an entity (0 if not animated). |
| `get_animation_clip_name(entity, clip_index)` | str | Get animation clip name by index. |
| `get_animation_clip_duration(entity, clip_index)` | float | Get animation clip duration (seconds) by index. |
| `play_animation(entity, clip_index)` | None | Play an animation clip by index (resets time, sets playing). |
| `stop_animation(entity)` | None | Stop animation playback (pauses and resets time to 0). |
| `is_animation_playing(entity)` | bool | Check if animation is currently playing. |
| `get_animation_speed(entity)` | float | Get animation playback speed (default 1.0). |
| `set_animation_speed(entity, speed)` | None | Set animation playback speed. |
| `get_animation_time(entity)` | float | Get current animation time (seconds). |
| `set_animation_time(entity, time)` | None | Set current animation time (seconds). |
| `is_animation_looping(entity)` | bool | Check if animation is set to loop. |
| `set_animation_looping(entity, loop)` | None | Set animation looping. |
| `get_current_animation_clip(entity)` | int | Get current animation clip index (-1 if none). |
| `save_to_file(path)` | bool | Calls native `saveToFile`; see the class contract above. |
| `load_from_file(path)` | bool | Calls native `loadFromFile`; see the class contract above. |

<!-- END SOURCE API -->
