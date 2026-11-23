#version 330

in vec2 shape_position;
uniform vec4 health_color;
uniform float health_fraction;
uniform bool overlay_bar;

layout(location=0) out vec4 out_col;

void main() {
    if (overlay_bar) {
        if (abs(shape_position.x) > 0.93 || abs(shape_position.y) > 0.64) {
            out_col = vec4(0.06, 0.06, 0.08, 1.0);
        }
        else {
            float fill_x = (shape_position.x + 0.93) / 1.86;
            out_col = fill_x <= health_fraction ? health_color : vec4(0.17, 0.17, 0.19, 1.0);
        }
    }
    else {
        float radius = length(shape_position);
        float edge = max(fwidth(radius), 0.01);
        float coverage = smoothstep(0.83 - edge, 0.83 + edge, radius) *
                         (1.0 - smoothstep(0.97 - edge, 0.97 + edge, radius));
        if (coverage <= 0.0) {
            discard;
        }
        out_col = vec4(1.0, 1.0, 1.0, coverage);
    }
}
