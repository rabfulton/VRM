#version 330 core

in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vWorldPos;

uniform vec4 uBaseColor;
uniform sampler2D uBaseColorTex;
uniform bool uHasBaseColorTex;
uniform int uAlphaMode; // 0=OPAQUE, 1=MASK, 2=BLEND
uniform float uAlphaCutoff;

out vec4 FragColor;

void main() {
    vec4 color = uBaseColor;
    if (uHasBaseColorTex) {
        color *= texture(uBaseColorTex, vTexCoord);
    }

    // Simple lighting
    vec3 normal = normalize(gl_FrontFacing ? vNormal : -vNormal);
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4;
    color.rgb *= diff;

    // Alpha handling
    if (uAlphaMode == 1 && color.a < uAlphaCutoff) {
        discard;
    }

    // Premultiplied alpha output
    FragColor = color;
}
