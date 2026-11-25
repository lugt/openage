#version 330

in vec2 tex_pos;
in vec2 tile_pos;
in vec2 blend_weights;

layout(location=0) out vec4 out_col;

uniform sampler2D tex;
uniform sampler2D fog_map;
uniform bool fog_enabled;

void main()
{
	if (blend_weights.x <= 0.0) {
		discard;
	}
	vec4 tex_val = texture(tex, tex_pos);
	// Alpha-over with cumulative weights yields an order-stable weighted mix.
	tex_val.a *= blend_weights.x / max(blend_weights.y, 0.00001);
	if (fog_enabled) {
		// Interpolate cell-center samples so contours can cross tile interiors.
		vec2 sample_pos = tile_pos - vec2(0.5);
		ivec2 cell = ivec2(floor(sample_pos));
		ivec2 limit = textureSize(fog_map, 0) - 1;
		vec2 f = smoothstep(vec2(0.0), vec2(1.0), fract(sample_pos));
		vec2 a = texelFetch(fog_map, clamp(cell, ivec2(0), limit), 0).rg;
		vec2 b = texelFetch(fog_map, clamp(cell + ivec2(1, 0), ivec2(0), limit), 0).rg;
		vec2 c = texelFetch(fog_map, clamp(cell + ivec2(0, 1), ivec2(0), limit), 0).rg;
		vec2 d = texelFetch(fog_map, clamp(cell + ivec2(1, 1), ivec2(0), limit), 0).rg;
		vec2 coverage = smoothstep(vec2(0.42), vec2(0.58), mix(mix(a, b, f.x), mix(c, d, f.x), f.y));
		float visibility = mix(coverage.r * (100.0 / 255.0), 1.0, coverage.g);
		tex_val.rgb *= visibility;
	}
	out_col = tex_val;
}
