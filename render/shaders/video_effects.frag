#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 sourceSize;
    float transition_kind;
    float transition_progress;
    float transition_softness;
    float transition_complexity;
    float transition_reveal;
    float saturation;
    float brightness;
    float blur;
    float vignette;
    float contrast_enabled;
    float contrast_amount;
    float contrast_pivot;
    float hue_shift_enabled;
    float hue_shift_angle;
    float hue_shift_saturation;
    float temperature_enabled;
    float temperature_temperature;
    float temperature_tint;
    float gamma_enabled;
    float gamma_gamma;
    float gamma_weight;
    float sepia_enabled;
    float sepia_amount;
    float sepia_warmth;
    float invert_enabled;
    float invert_amount;
    float posterize_enabled;
    float posterize_levels;
    float posterize_amount;
    float threshold_enabled;
    float threshold_threshold;
    float threshold_softness;
    float pixelate_enabled;
    float pixelate_size;
    float pixelate_amount;
    float sharpen_enabled;
    float sharpen_amount;
    float sharpen_radius;
    float chromatic_shift_enabled;
    float chromatic_shift_offset;
    float chromatic_shift_angle;
    float ripple_glass_enabled;
    float ripple_glass_amplitude;
    float ripple_glass_frequency;
    float digital_tear_enabled;
    float digital_tear_amount;
    float digital_tear_bands;
    float kaleidoscope_enabled;
    float kaleidoscope_segments;
    float kaleidoscope_mix;
    float halation_enabled;
    float halation_radius;
    float halation_amount;
    float duotone_enabled;
    float duotone_hue;
    float duotone_amount;
    float scanline_glow_enabled;
    float scanline_glow_spacing;
    float scanline_glow_amount;
    float solarize_enabled;
    float solarize_threshold;
    float solarize_amount;
    float emboss_light_enabled;
    float emboss_light_depth;
    float emboss_light_distance;
    float edge_neon_enabled;
    float edge_neon_threshold;
    float edge_neon_amount;
};

layout(binding = 1) uniform sampler2D source;

vec4 sampleSource(vec2 uv) {
    return texture(source, clamp(uv, vec2(0.0), vec2(1.0)));
}

float transitionCoordinate(vec2 uv) {
    float kind = transition_kind;
    float count = max(2.0, transition_complexity);
    if (kind < 1.5) return uv.x;
    if (kind < 2.5) return 1.0 - uv.x;
    if (kind < 3.5) return uv.y;
    if (kind < 4.5) return 1.0 - uv.y;
    if (kind < 5.5) return (uv.x + uv.y) * 0.5;
    if (kind < 6.5) return 2.0 * abs(uv.x - 0.5);
    if (kind < 7.5) return length(uv - vec2(0.5)) / 0.70710678;
    if (kind < 8.5) return abs(uv.x - 0.5) + abs(uv.y - 0.5);
    if (kind < 9.5) return fract(uv.x * count);
    if (kind < 10.5) return mod(floor(uv.x * count) + floor(uv.y * count), 2.0) * 0.5
        + fract(uv.x * count) * 0.5;
    if (kind < 11.5) return uv.x + 0.16 * sin(uv.y * 18.849556 + uv.x * 6.283185);
    if (kind < 12.5) return fract(length(uv - vec2(0.5)) * count);
    if (kind < 13.5) return uv.x + 0.13 * sin(uv.y * count * 3.141593);
    if (kind < 14.5) return mod(floor(uv.x * count) + floor(uv.y * count) * 3.0, 5.0) / 5.0
        + fract((uv.x + uv.y) * count) / 5.0;
    if (kind < 15.5) return mod(floor(uv.x * count) + floor(uv.y * count / 1.732) * 2.0, 7.0) / 7.0
        + fract(uv.x * count + mod(floor(uv.y * count / 1.732), 2.0) * 0.5) / 7.0;
    if (kind < 16.5) return abs(uv.x - 0.5) + abs(uv.y - 0.5)
        + 0.12 * sin((uv.x - uv.y) * count * 3.141593);
    if (kind < 17.5) return mod(floor(uv.x * count) * 7.0 + floor(uv.y * count) * 11.0, 13.0) / 13.0
        + fract(uv.x * count) / 13.0;
    if (kind < 18.5) return fract(uv.y * count) * 0.55 + uv.x * 0.45
        + 0.08 * sin(uv.y * count * 6.283185);
    if (kind < 19.5) return length(uv - vec2(0.5)) / 0.70710678
        + 0.13 * sin(uv.x * count * 3.141593) * sin(uv.y * count * 3.141593);
    return min(fract(uv.x * count), fract(uv.y * count));
}

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 pixel = 1.0 / max(sourceSize, vec2(1.0));
    vec4 raw = sampleSource(uv);
    if (ripple_glass_enabled > 0.5) {
        vec2 displacement = vec2(ripple_glass_amplitude * sin(6.283185 * ripple_glass_frequency * uv.y),
            ripple_glass_amplitude * 0.5 * sin(6.283185 * ripple_glass_frequency * uv.x)) * pixel;
        raw = sampleSource(uv + displacement);
    }
    if (digital_tear_enabled > 0.5) {
        float band = floor(uv.y * digital_tear_bands);
        float shift = digital_tear_amount * sin(band * 78.233) * sin(band * 12.9898);
        raw = sampleSource(uv + vec2(shift * pixel.x, 0.0));
    }
    if (kaleidoscope_enabled > 0.5) {
        float x = abs(mod(uv.x * kaleidoscope_segments, 2.0) - 1.0);
        raw = sampleSource(vec2(mix(uv.x, x, kaleidoscope_mix / 100.0), uv.y));
    }
    if (chromatic_shift_enabled > 0.5) {
        vec2 offset = chromatic_shift_offset * vec2(cos(radians(chromatic_shift_angle)),
            sin(radians(chromatic_shift_angle))) * pixel;
        raw.r = sampleSource(uv - offset).r;
        raw.b = sampleSource(uv + offset).b;
    }
    if (pixelate_enabled > 0.5 && pixelate_amount > 0.0) {
        vec2 origin = (floor(uv * sourceSize / max(pixelate_size, 1.0))
            * max(pixelate_size, 1.0) + vec2(0.5)) * pixel;
        raw = mix(raw, sampleSource(origin), clamp(pixelate_amount / 100.0, 0.0, 1.0));
    }
    if (blur > 0.0) {
        vec2 radius = pixel * (1.0 + blur * 13.0);
        vec4 neighbors = sampleSource(uv + vec2(radius.x, 0.0))
            + sampleSource(uv - vec2(radius.x, 0.0))
            + sampleSource(uv + vec2(0.0, radius.y))
            + sampleSource(uv - vec2(0.0, radius.y));
        raw = mix(raw, neighbors * 0.25, clamp(blur, 0.0, 1.0));
    }
    if (sharpen_enabled > 0.5 && sharpen_amount > 0.0) {
        vec2 radius = pixel * max(sharpen_radius, 1.0);
        vec4 neighbors = sampleSource(uv + vec2(radius.x, 0.0))
            + sampleSource(uv - vec2(radius.x, 0.0))
            + sampleSource(uv + vec2(0.0, radius.y))
            + sampleSource(uv - vec2(0.0, radius.y));
        raw.rgb += sharpen_amount / 100.0 * (raw.rgb * 4.0 - neighbors.rgb);
    }
    float alpha = raw.a;
    vec3 color = alpha > 0.0 ? raw.rgb / alpha : vec3(0.0);
    float luminance = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luminance), color, 1.0 + saturation);
    color += brightness;
    if (contrast_enabled > 0.5)
        color = (color - contrast_pivot / 100.0) * (contrast_amount / 100.0) + contrast_pivot / 100.0;
    if (hue_shift_enabled > 0.5) {
        vec3 yiq = vec3(dot(color, vec3(0.299, 0.587, 0.114)),
                        dot(color, vec3(0.596, -0.274, -0.322)),
                        dot(color, vec3(0.211, -0.523, 0.312)));
        float angle = radians(hue_shift_angle);
        vec2 chroma = mat2(cos(angle), sin(angle), -sin(angle), cos(angle)) * yiq.yz
            * (hue_shift_saturation / 100.0);
        color = vec3(yiq.x + 0.956 * chroma.x + 0.621 * chroma.y,
                     yiq.x - 0.272 * chroma.x - 0.647 * chroma.y,
                     yiq.x - 1.106 * chroma.x + 1.703 * chroma.y);
    }
    if (temperature_enabled > 0.5)
        color += vec3(temperature_temperature * 0.6 + temperature_tint * 0.15,
                      -temperature_tint * 0.45,
                      -temperature_temperature * 0.6 + temperature_tint * 0.15) / 255.0;
    if (gamma_enabled > 0.5) {
        vec3 corrected = pow(max(color, vec3(0.0)), vec3(100.0 / max(gamma_gamma, 1.0)));
        color = mix(color, corrected, clamp(gamma_weight / 100.0, 0.0, 1.0));
    }
    if (sepia_enabled > 0.5) {
        vec3 sepiaColor = vec3(dot(color, vec3(0.393, 0.769, 0.189)),
                               dot(color, vec3(0.349, 0.686, 0.168)),
                               dot(color, vec3(0.272, 0.534, 0.131)));
        sepiaColor += vec3(sepia_warmth * 0.3, 0.0, -sepia_warmth * 0.3) / 255.0;
        color = mix(color, sepiaColor, clamp(sepia_amount / 100.0, 0.0, 1.0));
    }
    if (invert_enabled > 0.5)
        color = mix(color, vec3(1.0) - color, clamp(invert_amount / 100.0, 0.0, 1.0));
    if (posterize_enabled > 0.5) {
        float steps = max(posterize_levels - 1.0, 1.0);
        color = mix(color, floor(color * steps + 0.5) / steps,
                    clamp(posterize_amount / 100.0, 0.0, 1.0));
    }
    if (threshold_enabled > 0.5) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        float thresholdValue = threshold_threshold / 255.0;
        float width = max(threshold_softness, 1.0) / 255.0;
        color = vec3(clamp((gray - thresholdValue + width * 0.5) / width, 0.0, 1.0));
    }
    if (halation_enabled > 0.5) {
        vec2 radius = pixel * halation_radius;
        float glow = max(0.0, (sampleSource(uv + vec2(radius.x, 0)).r
            + sampleSource(uv - vec2(radius.x, 0)).r
            + sampleSource(uv + vec2(0, radius.y)).r
            + sampleSource(uv - vec2(0, radius.y)).r) / 4.0 - 135.0 / 255.0);
        color += vec3(glow * halation_amount / 100.0,
                      glow * halation_amount / 220.0, 0.0);
    }
    if (duotone_enabled > 0.5) {
        float gray = dot(color, vec3(0.299, 0.587, 0.114));
        float angle = radians(duotone_hue);
        vec3 shadow = vec3(cos(angle), cos(angle - 2.094395), cos(angle + 2.094395));
        shadow = (0.5 + 0.5 * shadow) * (110.0 / 255.0);
        color = mix(color, mix(shadow, vec3(1.0), gray), duotone_amount / 100.0);
    }
    if (scanline_glow_enabled > 0.5) {
        float line = 0.7 + 0.3 * cos(6.283185 * uv.y * sourceSize.y / max(scanline_glow_spacing, 1.0));
        color *= mix(1.0, line, scanline_glow_amount / 100.0);
    }
    if (solarize_enabled > 0.5) {
        vec3 inverse = mix(color, 1.0 - color,
            step(vec3(solarize_threshold / 255.0), color));
        color = mix(color, inverse, solarize_amount / 100.0);
    }
    if (emboss_light_enabled > 0.5) {
        vec2 radius = pixel * emboss_light_distance;
        float relief = dot(sampleSource(uv - radius).rgb - sampleSource(uv + radius).rgb,
            vec3(0.299, 0.587, 0.114));
        color = mix(color, vec3(0.5 + relief), emboss_light_depth / 100.0);
    }
    if (edge_neon_enabled > 0.5) {
        vec2 radius = vec2(2.0 * pixel.x, 0.0);
        float edge = max(0.0, abs(dot(sampleSource(uv + radius).rgb
            - sampleSource(uv - radius).rgb, vec3(0.299, 0.587, 0.114)))
            - edge_neon_threshold / 255.0);
        color += edge * edge_neon_amount * vec3(1.0 / 250.0, 1.0 / 120.0, 1.0 / 80.0) * 255.0;
    }
    if (vignette > 0.0) {
        float edge = smoothstep(0.2, 0.72, length(uv - vec2(0.5)));
        color *= 1.0 - edge * clamp(vignette * 0.85, 0.0, 1.0);
    }
    if (transition_kind > 0.5) {
        float coordinate = clamp(transitionCoordinate(uv), 0.0, 1.0);
        if (transition_kind > 10.5) {
            vec3 accent = vec3(0.75, 0.93, 1.0);
            if (transition_kind < 11.5) accent = vec3(0.18, 0.82, 1.0);
            else if (transition_kind < 12.5) accent = vec3(0.40, 0.65, 1.0);
            else if (transition_kind < 13.5) accent = vec3(1.0, 0.55, 0.96);
            else if (transition_kind < 14.5) accent = vec3(1.0, 0.94, 0.78);
            else if (transition_kind < 15.5) accent = vec3(1.0, 0.64, 0.27);
            else if (transition_kind < 16.5) accent = vec3(1.0, 0.82, 0.38);
            else if (transition_kind < 17.5) accent = vec3(1.0, 0.30, 0.73);
            else if (transition_kind < 18.5) accent = vec3(0.70, 0.49, 1.0);
            else if (transition_kind < 19.5) accent = vec3(0.76, 0.91, 0.73);
            float phase = sin(3.14159265 * clamp(transition_progress, 0.0, 1.0));
            float edge = max(0.0, 1.0 - abs(coordinate - transition_progress) / 0.075);
            color += accent * (edge * phase * 0.85);
        }
        float delta = transition_reveal > 0.5
            ? transition_progress - coordinate : coordinate - transition_progress;
        alpha *= clamp(delta / max(transition_softness / 100.0, 0.000001) + 0.5, 0.0, 1.0);
    }
    fragColor = vec4(clamp(color, 0.0, 1.0) * alpha, alpha) * qt_Opacity;
}
