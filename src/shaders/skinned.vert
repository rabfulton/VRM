#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in ivec4 aJoints;
layout(location = 4) in vec4 aWeights;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
uniform mat4 uBones[128];
uniform bool uUseSkinning;

out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vWorldPos;

void main() {
    mat4 skinMatrix;
    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {
        skinMatrix = aWeights.x * uBones[aJoints.x]
                   + aWeights.y * uBones[aJoints.y]
                   + aWeights.z * uBones[aJoints.z]
                   + aWeights.w * uBones[aJoints.w];
    } else {
        skinMatrix = mat4(1.0);
    }

    vec4 worldPos = uModel * skinMatrix * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel * skinMatrix))) * aNormal;
    vTexCoord = aTexCoord;

    gl_Position = uProj * uView * worldPos;
}
