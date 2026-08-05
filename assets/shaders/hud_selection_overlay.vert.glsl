#version 330

// P4.4D.2 / P4.4F: world-space selection overlay vertex shader.
// Draws a ground-aligned quad centered on the entity's world position,
// scaled by half_size (in tile units). The quad corners are offset in
// the world se/ne plane (y=0 so the outline lies flat on the ground)
// and then projected through the camera. This yields a correct
// footprint-sized outline that follows the entity and scales with zoom.

layout(location=0) in vec2 v_position; // quad coords in {-1, 1}

// camera parameters (same std140 layout as world2d.vert.glsl).
// inv_zoom / inv_viewport_size are unused by this shader but kept in
// the UBO layout for compatibility; view and proj project the corner.
layout (std140) uniform camera {
    // view matrix (world to view space)
    mat4  view;
    // projection matrix (view to clip space)
    mat4  proj;
    // inverse zoom factor (1.0 / zoom) -- unused here
    float inv_zoom;
    // inverse viewport size (1.0 / viewport size) -- unused here
    vec2  inv_viewport_size;
};

// position of the entity in world (scene3) space
uniform vec3 obj_world_position;

// half-extent of the overlay quad in world (tile) units
uniform vec2 half_size;

void main() {
    // Ground-aligned quad: offset in the world se/ne plane by half_size tiles.
    // obj_world_position is scene3 world space (tiles); half_size is in tiles.
    vec3 corner = obj_world_position
                + vec3(v_position.x * half_size.x, 0.0, v_position.y * half_size.y);
    gl_Position = proj * view * vec4(corner, 1.0);
}

