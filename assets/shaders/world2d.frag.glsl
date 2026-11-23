#version 330

in vec2 vert_uv;

layout(location=0) out vec4 col;
layout(location=1) out uint id;

uniform sampler2D tex;
uniform uint u_id;
uniform uint player_color;
uniform bool fog_hidden;

const vec3 team_colors[8] = vec3[8](
	vec3(0.20, 0.45, 1.00), vec3(1.00, 0.20, 0.16),
	vec3(0.25, 0.75, 0.28), vec3(1.00, 0.86, 0.20),
	vec3(1.00, 0.55, 0.15), vec3(0.20, 0.83, 0.82),
	vec3(0.74, 0.39, 0.92), vec3(0.67, 0.67, 0.67)
);

// position (top left corner) and size: (x, y, width, height)
uniform vec4 tile_params;

vec2 uv = vec2(
	vert_uv.x * tile_params.z + tile_params.x,
	vert_uv.y * tile_params.w + tile_params.y
);

void main() {
	if (fog_hidden) {
		discard;
	}
	vec4 tex_val = texture(tex, uv);
	int alpha = int(round(tex_val.a * 255));
	// NOTE: written as if/else instead of a switch statement because the
	// macOS OpenGL driver crashes (SIGSEGV in glpLLVMCGSwitchStatement during
	// glLinkProgram) when compiling a switch statement here. See PR #1777.
	if (alpha == 0) {
		col = tex_val;
		discard;
		return;
	} else if (alpha == 254 || alpha == 252) {
		vec3 tint = team_colors[player_color % 8u];
		float shade = alpha == 252 ? 0.28 : mix(0.4, 1.0, tex_val.g);
		col = vec4(tint * shade, 1.0);
	} else if (alpha == 250) {
		col = vec4(0.0f, 0.0f, 1.0f, 1.0f);
	} else {
		col = tex_val;
	}
	id = u_id;
}
