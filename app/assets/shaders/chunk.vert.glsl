#version 330 core

// Packed chunk vertex attributes - see engine/include/voxels/render/chunk_vertex.hpp.
layout(location = 0) in vec3 aPosition;   // fixed-point local position, blocks * 16
layout(location = 1) in vec4 aFaceAoLight; // face index, ao (0..3), skyLight (0..15), blockLight (0..15)
layout(location = 2) in float aAtlasLayer;
layout(location = 3) in vec4 aUvTint;     // u, v (whole tile units, GL_REPEAT tiles merged quads), tint, reserved

uniform mat4 uViewProj;
uniform vec3 uChunkOrigin;

out vec2 vUV;
out float vLayer;
out float vAO;
out float vSkyLight;
out float vBlockLight;
out float vBrightness;
out float vTint;
out vec3 vWorldPos;

const vec3 kFaceNormal[6] = vec3[6](
    vec3(1.0, 0.0, 0.0), vec3(-1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0), vec3(0.0, -1.0, 0.0),
    vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, -1.0));
const float kFaceAmbient[6] = float[6](0.8, 0.8, 1.0, 0.5, 0.8, 0.8);

void main() {
    vec3 localPos = aPosition / 16.0;
    vec3 worldPos = uChunkOrigin + localPos;
    vWorldPos = worldPos;

    vUV = aUvTint.xy;
    vLayer = aAtlasLayer;
    vAO = aFaceAoLight.y;
    vSkyLight = aFaceAoLight.z;
    vBlockLight = aFaceAoLight.w;
    vTint = aUvTint.z;

    int faceIdx = int(aFaceAoLight.x + 0.5);
    vec3 sunDir = normalize(vec3(-0.6, 1.0, -0.5));
    float sunTerm = max(dot(kFaceNormal[faceIdx], sunDir), 0.0);
    vBrightness = kFaceAmbient[faceIdx] * (0.55 + 0.45 * sunTerm);

    gl_Position = uViewProj * vec4(worldPos, 1.0);
}
