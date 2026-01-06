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
uniform bool uUseBoneTexture;
uniform sampler2D uBoneTex;

out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vWorldPos;

void main() {
    mat4 skinMatrix;
    if (uUseSkinning && (aWeights.x + aWeights.y + aWeights.z + aWeights.w) > 0.0) {
        if (uUseBoneTexture) {
            ivec2 row = ivec2(0, aJoints.x);
            mat4 b0 = mat4(texelFetch(uBoneTex, row, 0),
                          texelFetch(uBoneTex, row + ivec2(1,0), 0),
                          texelFetch(uBoneTex, row + ivec2(2,0), 0),
                          texelFetch(uBoneTex, row + ivec2(3,0), 0));
            row = ivec2(0, aJoints.y);
            mat4 b1 = mat4(texelFetch(uBoneTex, row, 0),
                          texelFetch(uBoneTex, row + ivec2(1,0), 0),
                          texelFetch(uBoneTex, row + ivec2(2,0), 0),
                          texelFetch(uBoneTex, row + ivec2(3,0), 0));
            row = ivec2(0, aJoints.z);
            mat4 b2 = mat4(texelFetch(uBoneTex, row, 0),
                          texelFetch(uBoneTex, row + ivec2(1,0), 0),
                          texelFetch(uBoneTex, row + ivec2(2,0), 0),
                          texelFetch(uBoneTex, row + ivec2(3,0), 0));
            row = ivec2(0, aJoints.w);
            mat4 b3 = mat4(texelFetch(uBoneTex, row, 0),
                          texelFetch(uBoneTex, row + ivec2(1,0), 0),
                          texelFetch(uBoneTex, row + ivec2(2,0), 0),
                          texelFetch(uBoneTex, row + ivec2(3,0), 0));
            skinMatrix = aWeights.x * b0 + aWeights.y * b1 + aWeights.z * b2 + aWeights.w * b3;
        } else {
            skinMatrix = aWeights.x * uBones[aJoints.x]
                       + aWeights.y * uBones[aJoints.y]
                       + aWeights.z * uBones[aJoints.z]
                       + aWeights.w * uBones[aJoints.w];
        }
    } else {
        skinMatrix = mat4(1.0);
    }

    vec4 worldPos = uModel * skinMatrix * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(transpose(inverse(uModel * skinMatrix))) * aNormal;
    vTexCoord = aTexCoord;

    gl_Position = uProj * uView * worldPos;
}
