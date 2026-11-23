#version 330

layout(location=0) in vec2 v_position;
out vec2 shape_position;

layout (std140) uniform camera {
    mat4 view;
    mat4 proj;
    float inv_zoom;
    vec2 inv_viewport_size;
};

uniform vec3 obj_world_position;
uniform vec2 half_size;
uniform float sprite_top;
uniform bool overlay_bar;

void main() {
    shape_position = v_position;
    if (overlay_bar) {
        float bar_width = clamp(28.0 * inv_zoom * max(1.0, half_size.x), 22.0, 76.0);
        vec2 offset = vec2(v_position.x * bar_width,
                           v_position.y * 6.0 + 2.0 * (sprite_top * inv_zoom + 7.0));
        gl_Position = proj * view * vec4(obj_world_position, 1.0);
        gl_Position.xy += offset * inv_viewport_size * gl_Position.w;
    }
    else {
        vec3 corner = obj_world_position + vec3(v_position.x * half_size.x, 0.0, v_position.y * half_size.y);
        gl_Position = proj * view * vec4(corner, 1.0);
    }
}
