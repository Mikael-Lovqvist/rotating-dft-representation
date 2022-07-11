#version 430

layout (location = 0) in vec3 tex_normal;
layout (location = 1) in vec2 tex_coord;
layout (location = 2) in float height;
layout (location = 3) in vec3 light_direction;
layout (location = 4) in mat4 frag_mat;


layout (location = 0) out vec4 diffuse_out;

uniform sampler2D tex0;
uniform vec2 mouse_coord;


//texelFetch is for direct access using non normalized coordinates

//uniform sampler2D tex0;
//uniform samplerBuffer tex0;


//varying vec3 normal_out;
//uniform vec3 light_direction;

float f = 0.1;

void main(void) {	
	
	//gl_FragColor = vec4(vec3(0.6,1.0,0.3) *  dot(light_direction, normal_out) , 1.0);
	
/*	float dx = texture2D(tex0, tex_coord).r - texture2D(tex0, tex_coord + vec2(0.01,0.0)).r;
	float dy = texture2D(tex0, tex_coord).r - texture2D(tex0, tex_coord + vec2(0.0,0.01)).r;

	diffuse_out = vec4(0.5 + dx*5, 0.5+dy*5, texture2D(tex0, tex_coord).r, 1.0);*/
/*	float h = texture2D(tex0, tex_coord).r;
	diffuse_out = vec4(h*7.0, h*3.0, h , 1.0);
*/	
	//diffuse_out = vec4(pow(height, 4), pow(height, 2)*4, pow(height, .5), 1.0);

	float real_light = dot(tex_normal, light_direction)*1.2+.2;
	float light = 1;//real_light;
	//diffuse_out = vec4(pow(height, 2), pow(height, 2)*4, 1-pow(abs(height), .5), 1.0) * light ;

	diffuse_out = vec4(-cos(0+height*7*f)*0.5+0.5, -cos(height*-13*f)*0.5+0.5, -cos(height*41*f)*0.5+0.5, 1.0) * light;

	

	//diffuse_out = vec4(gl_FragCoord.xyz * .001, 1);
}

