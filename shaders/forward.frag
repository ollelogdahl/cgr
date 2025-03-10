#version 450
#define M_PI 3.1415926535897932384626433832795

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 frag_normal_ws;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_vertex_color;
layout(location = 4) in flat uint material_id;

layout(location = 0) out vec4 outColor;

struct GlobalData {
    mat4 cam_view;
    mat4 cam_proj;
    vec3 cam_pos;
};

struct MaterialData {
    vec4 color;
    vec4 emission;
    float roughness;
    float metallic;
    float _pad[2];
};

struct LightData {
    vec4 position;
    vec4 color;
    float falloff_linear;
    float falloff_quadratic;
};

layout(set = 0, binding = 0) readonly buffer GlobalBuffer {
    GlobalData global;
};

layout(set = 0, binding = 2) readonly buffer MaterialBuffer {
    MaterialData materials[];
};

layout(set = 0, binding = 3) readonly buffer LightBuffer {
    uint light_count;
    LightData lights[];
};

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distribution_ggx(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = M_PI * denom * denom;

    return num / denom;
}

float geometry_schlick_ggx(float dot, float k)
{
    float num = dot;
    float denom = dot * (1.0 - k) + k;

    return num / denom;
}
float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float k = roughness * roughness / 2.0;

    float ggx2 = geometry_schlick_ggx(NdotV, k);
    float ggx1 = geometry_schlick_ggx(NdotL, k);

    return ggx1 * ggx2;
}

vec3 lambert(vec3 C) {
    return C / M_PI;
}

vec3 cook_torrance(vec3 L, vec3 V, vec3 N, float D, float G, vec3 F) {
    return D * G * F / (4.0 * max(dot(N, L), 0.0) * max(dot(N, V), 0.0) + 0.0001);
}

struct PbrProperties {
    vec3 albedo;
    float roughness;
    float metallic;
};

vec3 pbr(
    vec3 L,
    vec3 V,
    vec3 N,

    PbrProperties props,

    // should be the incoming light
    vec3 radiance,
    float shadow
) {
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    float r = max(0.0001, props.roughness);

    // assuming that non-metal materials zero-incidence reflectance of 0.04
    vec3 F0 = mix(vec3(0.04), props.albedo, props.metallic);

    float D = distribution_ggx(N, H, r);
    float G = geometry_smith(N, V, L, r);
    vec3 F = fresnel_schlick(HdotV, vec3(0.04));

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - props.metallic;

    vec3 specular = cook_torrance(L, V, N, D, G, F);

    vec3 diffuse = lambert(props.albedo);

    return (kD * diffuse + specular) * radiance * NdotL * shadow;
}

void main() {
    vec3 P = frag_pos_ws;
    vec3 V = normalize(global.cam_pos - frag_pos_ws);
    vec3 N = normalize(frag_normal_ws);
    vec3 color = vec3(0.0);

    PbrProperties props = PbrProperties(
        materials[material_id].color.rgb,
        materials[material_id].roughness,
        materials[material_id].metallic);

    for (uint i = 0; i < light_count; i++) {
        LightData light = lights[i];

        bool light_is_directional = light.position.w == 0.0;
        vec3 light_dir_point = normalize(light.position.xyz - P);
        vec3 light_dir_dir = normalize(-light.position.xyz);
        vec3 L = light_is_directional ? light_dir_dir : light_dir_point;

        float distance = length(P - light.position.xyz);
        float point_attenuation = 1.0 / (1.0 + light.falloff_linear * distance + light.falloff_quadratic * pow(distance, 2.0));

        vec3 radiance_point = (light.color.rgb * light.color.a) * point_attenuation;
        vec3 radiance_dir = light.color.rgb * light.color.a;
        vec3 radiance = light_is_directional ? radiance_dir : radiance_point;
        float shadow = 1.0;

        vec3 result = pbr(
            L,
            V,
            N,
            props,
            radiance,
            shadow
        );

        color += result;
    }

    outColor = vec4(color, 1.0);
}
