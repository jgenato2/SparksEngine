#version 460 core

// Raymarching cloud shadow map from sun's view
layout(location = 0) out float fragShadow;

uniform mat4 uSunViewProj;
uniform float uTime;
uniform float uCloudBase;
uniform float uCloudTop;
uniform float uCloudDensity;

// Simple 3D noise (replace with better noise for production)
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    float n = mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                     mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
                 mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                     mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
    return n;
}

float cloudDensity(vec3 p) {
    float h = (p.y - uCloudBase) / (uCloudTop - uCloudBase);
    if (h < 0.0 || h > 1.0) return 0.0;
    float d = noise(p * 0.04 + uTime * 0.01) * 0.7 + noise(p * 0.12 - uTime * 0.02) * 0.3;
    d = smoothstep(0.45, 0.65, d) * h * (1.0 - h);
    return d * uCloudDensity;
}

void main() {
    // Reconstruct world position from sun view
    vec2 uv = gl_FragCoord.xy / textureSize(gl_FragCoord, 0);
    vec4 sunView = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    vec4 worldPos = inverse(uSunViewProj) * sunView;
    worldPos /= worldPos.w;
    vec3 rayOrigin = worldPos.xyz;
    vec3 rayDir = vec3(0, -1, 0); // Sun down
    float t = 0.0;
    float shadow = 1.0;
    for (int i = 0; i < 64; ++i) {
        vec3 p = rayOrigin + rayDir * t;
        float d = cloudDensity(p);
        shadow *= exp(-d * 0.18);
        t += 1.5;
        if (p.y < uCloudBase) break;
    }
    fragShadow = shadow;
}
