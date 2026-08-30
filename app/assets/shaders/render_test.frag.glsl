#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vWorldPos;

uniform vec3 uCameraPos;
uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(vec3(-0.6, 1.0, -0.5));
    float diff = max(dot(normal, lightDir), 0.0);
    vec2 uv = vTexCoord;
    vec3 baseColor = texture(uTexture, uv).rgb;
    float fog = clamp((length(vWorldPos - uCameraPos) - 2.0) / 18.0, 0.0, 1.0);
    vec3 lit = baseColor * (0.35 + diff * 0.85);
    vec3 skyTint = vec3(0.55, 0.70, 0.92);
    FragColor = vec4(mix(lit, skyTint, fog), 1.0);
}
