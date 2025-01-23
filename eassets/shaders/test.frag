#version 460

#define M_PI 3.1415926535897932384626433832795

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 normal_ws;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform Env {
    mat4 cam_view;
    mat4 cam_proj;
    vec3 cam_pos;
} env;

layout(push_constant) uniform PushConsts {
    layout(offset = 64) float color_r;
    layout(offset = 68) float color_g;
    layout(offset = 72) float color_b;
    layout(offset = 76) float roughness;
    layout(offset = 80) float metallic;
} element;

vec3 fresnel_schlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float distribution_ggx(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = M_PI * denom * denom;

    return num / denom;
}

float geometry_schlick_ggx(float dot, float k)
{
    float num   = dot;
    float denom = dot * (1.0 - k) + k;

    return num / denom;
}
float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);

    float k = roughness * roughness / 2.0;

    float ggx2  = geometry_schlick_ggx(NdotV, k);
    float ggx1  = geometry_schlick_ggx(NdotL, k);

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

struct PointLight {
    vec3 position;
    vec3 color;
    float linear;
    float quadratic;
};

float calculate_attenuation(PointLight pl, vec3 P) {
    float dist = length(pl.position - P);

    return 1.0 / (1.0 + pl.linear * dist + pl.quadratic * dist * dist);
}

vec3 calculate_pl_radiance(PointLight pl, vec3 P) {
    float attenuation = calculate_attenuation(pl, P);
    return pl.color * attenuation;
}

void main() {
    PointLight pl = PointLight(
        vec3(0.0, 1.0, 1.0),
        vec3(1.0, 1.0, 1.0),
        0.09,
        0.032
    );

    vec3 P = frag_pos_ws;
    vec3 L = normalize(pl.position - frag_pos_ws);
    vec3 V = normalize(env.cam_pos - frag_pos_ws);
    vec3 N = normalize(normal_ws);


    PbrProperties props = PbrProperties(
        vec3(element.color_r, element.color_g, element.color_b),
        element.roughness,
        element.metallic
    );

    vec3 color = pbr(L, V, N, props, calculate_pl_radiance(pl, P), 1.0);

    outColor = vec4(color, 1.0);
}
