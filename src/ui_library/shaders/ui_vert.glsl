#version 330 core
layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 texcoord;
layout (location = 2) in vec4 color;
uniform mat4 project;
out vec4 Frag_Color;
out vec4 position;
out vec2 tcoord;

void main(){
	Frag_Color = color;
	tcoord = texcoord.st;
	
	gl_Position = project * vec4(pos.xy, 0.0, 1.0);
}

