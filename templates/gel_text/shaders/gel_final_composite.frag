#version 450 core
in vec2 v_uv;
layout(location = 0) out vec4 fragColor;

uniform sampler2D u_textSceneTex; // Buffer HDR con testo solido e lama speculare
uniform sampler2D u_sdfTex;        // Texture SDF per sagoma e ombra
uniform sampler2D u_backgroundTex; // Immagine o video di fondo

uniform vec2  u_center = vec2(0.5, 0.5);
uniform float u_zoomIntensity = 0.038;
uniform float u_chromaAmount  = 0.006;

// 1. Funzione Radial Zoom Blur (allungamento dinamico sui bordi esterni)
vec3 getZoomBlur(sampler2D tex, vec2 uv, vec2 center, float intensity) {
    vec2 dir = uv - center;
    float dist = length(dir);
    float blurFactor = smoothstep(0.08, 0.65, dist) * intensity;
    
    vec3 accum = vec3(0.0);
    const int N = 14;
    for(int i = 0; i < N; i++) {
        float factor = 1.0 - blurFactor * (float(i) / float(N - 1));
        accum += texture(tex, center + dir * factor).rgb;
    }
    return accum / float(N);
}

// 2. Soft Drop Shadow calcolata da SDF
float computeDropShadow(sampler2D sdfTex, vec2 uv) {
    vec2 shadowOffset = vec2(0.002, -0.007);
    float dShadow = texture(sdfTex, uv - shadowOffset).r;
    float shadowRadius = 0.08;
    float shadowAlpha = smoothstep(0.5 - shadowRadius, 0.5, dShadow);
    return shadowAlpha * 0.65;
}

void main() {
    // A. Calcolo dell'Ombra Morbida sullo Sfondo
    float shadow = computeDropShadow(u_sdfTex, v_uv);
    vec3 bg = texture(u_backgroundTex, v_uv).rgb;
    vec3 bgWithShadow = bg * (1.0 - shadow * 0.75);

    // B. Campionamento del Testo con Aberrazione Cromatica Radiale
    vec2 toCenter = v_uv - u_center;
    float distSq = dot(toCenter, toCenter);
    vec2 chromaDisp = normalize(toCenter) * distSq * u_chromaAmount + vec2(0.0008, 0.0015);

    float r = texture(u_textSceneTex, v_uv + chromaDisp).r;
    float g = texture(u_textSceneTex, v_uv).g;
    float b = texture(u_textSceneTex, v_uv - chromaDisp).b;
    vec3 textChromatic = vec3(r, g, b);
    float textAlpha = texture(u_textSceneTex, v_uv).a;

    // C. Radial Zoom Blur sull'Outer Glow / Bagliore Etereo
    vec3 radialGlow = getZoomBlur(u_textSceneTex, v_uv, u_center, u_zoomIntensity);
    vec3 stretchedGlow = radialGlow * vec3(0.0, 0.75, 1.0) * 1.3;

    // D. Compositing Stratificato:
    // 1. Sfondo con Ombra
    // 2. + Radial Glow allungato verso i lati (additivo)
    // 3. + Testo con Aberrazione Cromatica (alpha blend)
    vec3 finalLinear = mix(bgWithShadow, textChromatic, textAlpha) + stretchedGlow * (1.0 - textAlpha * 0.5);

    // E. Tonemapping e Correzione Gamma sRGB
    vec3 finalColor = pow(clamp(finalLinear, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColor = vec4(finalColor, 1.0);
}
