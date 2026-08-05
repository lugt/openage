#version 330

// P4.4D.2 / P4.4F: world-space selection overlay vertex shader.
// Draws a quad centered on the entity's world position, scaled by
// half_size (in tile units). The camera UBO projects the center to
// clip space; the quad vertices are offset in clip space so the
// overlay follows the entity and scales with zoom. This mirrors the
// approach used by world2d.vert.glsl for sizing subtex quads.

layout(location=0) in vec2 v_position; // quad coords in {-1, 1}

// camera parameters (same std140 layout as world2d.vert.glsl)
layout (std140) uniform camera {
    // view matrix (world to view space)
    mat4  view;
    // projection matrix (view to clip space)
    mat4  proj;
    // inverse zoom factor (1.0 / zoom)
    float inv_zoom;
    // inverse viewport size (1.0 / viewport size)
    vec2  inv_viewport_size;
};

// position of the entity in world (scene3) space
uniform vec3 obj_world_position;

// half-extent of the overlay quad in world (tile) units
uniform vec2 half_size;

void main() {
    // project the entity's world position to clip space
    vec4 clip = proj * view * vec4(obj_world_position, 1.0);

    // scale the quad vertices by half_size in world units, then
    // convert to clip-space offset via inv_zoom and inv_viewport_size.
    // A vertex at v_position = +/-1 maps to +/- half_size world units.
    // The factor of 2.0 accounts for the clip-space range of [-1, 1].
    vec2 vert_scale = inv_zoom * half_size * inv_viewport_size * 2.0;

    gl_Position = clip + vec4(v_position * vert_scale, 0.0, 0.0);
}
