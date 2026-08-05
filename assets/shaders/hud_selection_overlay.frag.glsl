#version 330

// P4.4D.2 / P4.4F: world-space selection overlay fragment shader.
// Outputs the per-quad color (with alpha for blending over terrain).

uniform vec4 in_col;

layout(location=0) out vec4 out_col;

void main() {
    out_col = in_col;
}
