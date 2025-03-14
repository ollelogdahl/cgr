vec3 color_rgb_to_hsv(vec3 rgb) {
    float cmax = max(max(rgb.r, rgb.g), rgb.b);
    float cmin = min(min(rgb.r, rgb.g), rgb.b);
    float delta = cmax - cmin;

    float h = 0.0;
    if (delta != 0.0) {
        if (cmax == rgb.r) {
            h = mod((rgb.g - rgb.b) / delta, 6.0);
        } else if (cmax == rgb.g) {
            h = (rgb.b - rgb.r) / delta + 2.0;
        } else {
            h = (rgb.r - rgb.g) / delta + 4.0;
        }
    }

    h *= 60.0;
    if (h < 0.0) {
        h += 360.0;
    }

    float s = cmax == 0.0 ? 0.0 : delta / cmax;
    float v = cmax;

    return vec3(h, s, v);
}

vec3 color_hsv_to_rgb(vec3 hsv) {
vec3 rgb = vec3(0.0);

    float h = hsv.x * 6.0;
    float c = hsv.z * hsv.y;
    float x = c * (1.0 - abs(mod(h, 2.0) - 1.0));
    float m = hsv.z - c;

    if (h < 1.0) {
        rgb = vec3(c, x, 0.0);
    } else if (h < 2.0) {
        rgb = vec3(x, c, 0.0);
    } else if (h < 3.0) {
        rgb = vec3(0.0, c, x);
    } else if (h < 4.0) {
        rgb = vec3(0.0, x, c);
    } else if (h < 5.0) {
        rgb = vec3(x, 0.0, c);
    } else {
        rgb = vec3(c, 0.0, x);
    }

    return rgb + m;
}

vec3 color_random(uint seed) {
    // Constants for a good hash function
    const uint OFFSET_BASIS = 2166136261u;
    const uint FNV_PRIME = 16777619u;
    
    // FNV-1a hash for better distribution
    uint hash = OFFSET_BASIS;
    
    // Process each byte of the seed
    for (int i = 0; i < 4; i++) {
        uint byte = (seed >> (i * 8)) & 0xFFu;
        hash ^= byte;
        hash *= FNV_PRIME;
    }
    
    // Gold Noise technique - using irrational numbers to distribute values
    float phi = 1.61803398874989484820459; // Golden ratio
    float pi = 3.14159265358979323846264;  // Pi
    float e = 2.71828182845904523536029;   // Euler's number
    
    // Generate RGB components that are visually distinct
    float r = fract(cos(float(hash) * phi) * 43758.5453);
    float g = fract(cos(float(hash) * pi) * 43758.5453);
    float b = fract(cos(float(hash) * e) * 43758.5453);
    
    // Ensure minimum brightness
    float minBrightness = 0.2;
    r = max(r, minBrightness);
    g = max(g, minBrightness);
    b = max(b, minBrightness);
    
    return vec3(r, g, b);
}

vec3 color_range_viridis(float x) {
    x = clamp(x, 0.0, 1.0);

    // Constants for the color map
    vec3 c0 = vec3(0.267004, 0.004874, 0.329415);
    vec3 c1 = vec3(0.253935, 0.265254, 0.529983);
    vec3 c2 = vec3(0.163625, 0.471133, 0.558148);
    vec3 c3 = vec3(0.134692, 0.658636, 0.517649);
    vec3 c4 = vec3(0.477504, 0.821444, 0.318195);
    vec3 c5 = vec3(0.993248, 0.906157, 0.143936);

    // Determine which segment x falls in and interpolate
    float i = x * 5.0;
    float f = fract(i);
    int segment = int(floor(i));

    // Linear interpolation between appropriate colors
    vec3 color;
    if (segment == 0) {
        color = mix(c0, c1, f);
    } else if (segment == 1) {
        color = mix(c1, c2, f);
    } else if (segment == 2) {
        color = mix(c2, c3, f);
    } else if (segment == 3) {
        color = mix(c3, c4, f);
    } else {
        color = mix(c4, c5, f);
    }

    return color;
}
