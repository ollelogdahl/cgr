#version 450
#extension GL_EXT_nonuniform_qualifier : require

#define M_PI 3.1415926535897932384626433832795

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec4 frag_vertex_color;

layout(location = 0) out vec4 outColor;

struct PointLight {
    vec3 position;
    float linear;
    vec3 color;
    float quadratic;
};

layout(binding = 0) uniform Env {
    mat4 cam_view;
    mat4 cam_proj;
    vec3 cam_pos;
    uint num_point_lights;

    PointLight point_lights[16];
} env;

layout(push_constant) uniform PushConsts {
    layout(offset = 64) uint flags;
    layout(offset = 68) float color_r;
    layout(offset = 72) float color_g;
    layout(offset = 76) float color_b;
    layout(offset = 80) float roughness;
    layout(offset = 84) float metallic;
    layout(offset = 88) uint albedo0_idx;
    layout(offset = 92) uint albedo1_idx;
    layout(offset = 96) uint albedo2_idx;
    layout(offset = 100) uint normal_idx;
    layout(offset = 104) uint roughness_idx;
} element;

layout(set = 1, binding = 0) uniform sampler2D textures[];

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

float calculate_attenuation(PointLight pl, vec3 P) {
    float dist = length(pl.position - P);

    return 1.0 / (1.0 + pl.linear * dist + pl.quadratic * dist * dist);
}

vec3 calculate_pl_radiance(PointLight pl, vec3 P) {
    float attenuation = calculate_attenuation(pl, P);
    return pl.color * attenuation;
}

// Based on:
// Christian Schüler, “Normal Mapping without Precomputed Tangents”,
// ShaderX 5, Chapter 2.6, pp. 131 – 140
//
// taken from http://www.thetenthplanet.de/archives/1180

mat3 shuler_cotangent_frame(vec3 N, vec3 p, vec2 uv) {
    // get edge vectors of the pixel triangle
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // solve the linear system
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

vec3 shuler_perturb_normal(sampler2D bumpmap, vec3 N, vec3 V, vec2 texcoord) {
    // assume N, the interpolated vertex normal and
    // V, the view vector (vertex to eye)
    vec3 pn = texture(bumpmap, texcoord).xyz;

    pn = pn * 2. - 1.;
    // map = map * 255. / 127. - 128. / 127.;
    // pn.z = sqrt(1. - dot(pn.xy, pn.xy));
    pn.y = -pn.y;

    mat3 TBN = shuler_cotangent_frame(N, -V, texcoord);
    return normalize(TBN * pn);
}

void main() {
    vec3 albedo;
    if ((element.flags & 1) != 0) {
        if ((element.flags & 8) != 0) {
            vec3 a1 = texture(textures[element.albedo0_idx], frag_uv).rgb * frag_vertex_color.x;
            vec3 a2 = texture(textures[element.albedo1_idx], frag_uv).rgb * frag_vertex_color.y;
            vec3 a3 = texture(textures[element.albedo2_idx], frag_uv).rgb * frag_vertex_color.z;
            albedo = a1 + a2 + a3;
        } else {
            albedo = texture(textures[element.albedo0_idx], frag_uv).rgb;
        }
    } else {
        albedo = vec3(element.color_r, element.color_g, element.color_b);
    }

    float roughness;
    if ((element.flags & 4) != 0) {
        roughness = texture(textures[element.roughness_idx], frag_uv).r;
    } else {
        roughness = element.roughness;
    }

    PbrProperties props = PbrProperties(
            albedo,
            roughness,
            element.metallic
        );

    vec3 P = frag_pos_ws;
    vec3 V = normalize(env.cam_pos - frag_pos_ws);
    vec3 N = normalize(normal_ws);

    if ((element.flags & 2) != 0) {
        N = shuler_perturb_normal(textures[element.normal_idx], N, V, frag_uv);
    }

    vec4 out_color = vec4(0.0, 0.0, 0.0, 1.0);
    for (uint i = 0; i < env.num_point_lights; i++) {
        PointLight pl = env.point_lights[i];
        vec3 L = normalize(pl.position - frag_pos_ws);

        vec3 color = pbr(L, V, N, props, calculate_pl_radiance(pl, P), 1.0);
        out_color += vec4(color, 1.0);
    }

    float ambient = 0.02;
    out_color += vec4(ambient, ambient, ambient, 0.0);

    outColor = out_color;
}
