#version 330 core

in vec2 vUV;
in float vLayer;
in float vAO;
in float vSkyLight;
in float vBlockLight;
in float vSunTerm;
in float vTint;
in vec3 vWorldPos;

uniform sampler2DArray uTextureAtlas;
uniform vec3 uCameraPos;
uniform vec3 uFoliageTint;
uniform vec3 uSunColor;
uniform vec3 uAmbientColor;
uniform vec3 uSkyColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTextureAtlas, vec3(vUV, vLayer));
    if (texColor.a < 0.05) {
        discard;
    }

    vec3 baseColor = texColor.rgb;
    if (vTint > 0.5) {
        baseColor *= uFoliageTint;
    }

    float aoFactor = 0.35 + 0.65 * (vAO / 3.0);
    float skyFactor = clamp(vSkyLight / 15.0, 0.0, 1.0);
    float blockFactor = clamp(vBlockLight / 15.0, 0.0, 1.0);
    vec3 skyLighting = skyFactor * (uAmbientColor + uSunColor * vSunTerm);
    vec3 blockLighting = blockFactor * vec3(1.0, 0.58, 0.28);
    vec3 lit = baseColor * aoFactor * max(skyLighting + blockLighting, vec3(0.015));

    float fog = smoothstep(uFogStart, uFogEnd, length(vWorldPos.xz - uCameraPos.xz));
    FragColor = vec4(mix(lit, uSkyColor, fog), texColor.a);
}
