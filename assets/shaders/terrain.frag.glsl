#version 330

in vec2 tex_pos;
in vec2 tile_pos;

layout(location=0) out vec4 out_col;

uniform sampler2D tex;
uniform sampler2D fog_map;
uniform bool fog_enabled;

void main()
{
    vec4 tex_val = texture(tex, tex_pos);
	if (fog_enabled) {
		ivec2 tile = clamp(ivec2(floor(tile_pos)), ivec2(0), textureSize(fog_map, 0) - 1);
		float visibility = texelFetch(fog_map, tile, 0).r;
		tex_val.rgb = mix(vec3(0.035, 0.045, 0.05), tex_val.rgb, visibility);
	}
	out_col = tex_val;
}
