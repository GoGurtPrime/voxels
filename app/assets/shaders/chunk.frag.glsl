#version 330 core

in vec2 vUV;
in float vLayer;
in float vAO;
in float vSkyLight;
in float vBlockLight;
in float vBrightness;
in float vTint;
in vec3 vWorldPos;

uniform sampler2DArray uTextureAtlas;
uniform vec3 uCameraPos;
uniform vec3 uFoliageTint;

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
    float lightFactor = clamp(max(vSkyLight, vBlockLight) / 15.0, 0.15, 1.0);
    vec3 lit = baseColor * (aoFactor * lightFactor * vBrightness);

    float fog = clamp((length(vWorldPos - uCameraPos) - 24.0) / 48.0, 0.0, 1.0);
    vec3 skyTint = vec3(0.55, 0.70, 0.92);
    FragColor = vec4(mix(lit, skyTint, fog), texColor.a);
}
