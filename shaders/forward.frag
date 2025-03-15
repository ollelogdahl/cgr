#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_KHR_shader_subgroup_basic : enable
#extension GL_KHR_shader_subgroup_vote : enable
#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#include "cluster.glsl"
#include "color.glsl"

#define M_PI 3.1415926535897932384626433832795

layout(location = 0) in vec3 frag_pos_ws;
layout(location = 1) in vec3 frag_normal_ws;
layout(location = 2) in vec2 frag_uv;
layout(location = 3) in vec3 frag_vertex_color;
layout(location = 4) in flat uint material_id;

layout(location = 0) out vec4 outColor;

#define DEBUG_NONE 0
#define DEBUG_UNLIT 1
#define DEBUG_SKIP_CLUSTER_SHADING 2
#define DEBUG_CLUSTER_SCALAR_READ 3
#define DEBUG_CLUSTER_LIGHTS 4
#define DEBUG_CLUSTER_HASH 5

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

    float tex_scale;

    uint tex_albedo0;
    uint tex_albedo1;
    uint tex_albedo2;
    uint tex_normal;
    uint tex_metallic;
    uint tex_roughness;
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
    uint highest_light;
    LightData lights[];
};

layout(set = 0, binding = 4) readonly buffer ClusterBuffer {
    ClusterConstants cluster_constants;
    ClusterData clusters[];
};

layout(set = 0, binding = 5) readonly buffer ClusterItemBuffer {
    uint cluster_items_size;
    uint cluster_items[];
};

layout(set = 0, binding = 6) uniform sampler2D textures[];

layout(push_constant) uniform PushConstants {
    uint debug_mode;
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

    // flip as vulkan is y-down
    dp2 = -dp2;
    duv2 = -duv2;

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
    pn.y = -pn.y;

    mat3 TBN = shuler_cotangent_frame(N, -V, texcoord);
    return normalize(TBN * pn);
}

float point_light_attenuation(float dist, float falloff_linear, float falloff_quadratic) {
    return 1.0 / (1.0 + falloff_linear * dist + falloff_quadratic * pow(dist, 2.0));
}

vec3 point_light_shade(vec3 P, vec3 N, vec3 V, LightData light, PbrProperties props) {
    vec3 L = normalize(light.position.xyz - P);

    float dist = length(P - light.position.xyz);
    float attenuation = point_light_attenuation(dist, light.falloff_linear, light.falloff_quadratic);

    vec3 radiance = (light.color.rgb * light.color.a) * attenuation;

    vec3 result = pbr(
        L,
        V,
        N,
        props,
        radiance,
        1.0
    );

    return result;
}

void main() {
    vec3 P = frag_pos_ws;
    vec3 V = normalize(global.cam_pos - frag_pos_ws);
    vec3 N = normalize(frag_normal_ws);
    vec3 color = vec3(0.0);

    vec3 position_vs = (global.cam_view * vec4(P, 1.0)).xyz;

    vec2 tex_uv = frag_uv * materials[material_id].tex_scale;

    // determine if we use textures.
    uint normalmap_idx = materials[material_id].tex_normal;
    if (normalmap_idx != -1) {
        N = shuler_perturb_normal(textures[normalmap_idx], N, V, tex_uv);
    }

    uint albedo0_idx = materials[material_id].tex_albedo0;
    uint albedo1_idx = materials[material_id].tex_albedo1;
    uint albedo2_idx = materials[material_id].tex_albedo2;

    vec3 albedo = vec3(0.0);
    if (albedo0_idx != -1) {
        bool multitex = albedo0_idx != -1 && albedo1_idx != -1 && albedo2_idx != -1;

        if (multitex) {
            vec3 a0 = texture(textures[albedo0_idx], tex_uv).rgb * frag_vertex_color.x;
            vec3 a1 = texture(textures[albedo1_idx], tex_uv).rgb * frag_vertex_color.y;
            vec3 a2 = texture(textures[albedo2_idx], tex_uv).rgb * frag_vertex_color.z;
            albedo = a0 + a1 + a2;
        } else {
            albedo = texture(textures[albedo0_idx], tex_uv).rgb;
        }
    } else {
        albedo = materials[material_id].color.rgb;
    }

    uint roughness_idx = materials[material_id].tex_roughness;
    uint metallic_idx = materials[material_id].tex_metallic;

    float roughness = materials[material_id].roughness;
    float metallic = materials[material_id].metallic;

    if (roughness_idx != -1) {
        roughness = texture(textures[roughness_idx], tex_uv).r;
    }

    if (metallic_idx != -1) {
        metallic = texture(textures[metallic_idx], tex_uv).r;
    }

    if (debug_mode == DEBUG_UNLIT) {
        outColor = vec4(albedo, 1.0);
        return;
    }

    vec4 clip_pos = global.cam_proj * vec4(position_vs, 1.0);
    clip_pos /= clip_pos.w;

    uint cluster_id = cluster_lookup(clip_pos.xyz, position_vs, cluster_constants);

    // if (debug_mode == DEBUG_CLUSTER_ID) {
    //     outColor = vec4(color_random(cluster_id), 1.0);
    //     return;
    // }

    if (debug_mode == DEBUG_CLUSTER_LIGHTS) {
        uint num_lights = clusters[cluster_id].hash_and_num_lights & 0xFF;
        float occupancy = float(num_lights) / 30;

        outColor = vec4(color_range_viridis(occupancy), 1.0);
        return;
    }

    if (debug_mode == DEBUG_CLUSTER_HASH) {
        uint hash = clusters[cluster_id].hash_and_num_lights >> 8;

        outColor = vec4(color_random(hash), 1.0);
        return;
    }

    PbrProperties props = PbrProperties(
            albedo,
            roughness,
            metallic);


    if (debug_mode == DEBUG_SKIP_CLUSTER_SHADING) {
        for (uint i = 0; i < highest_light + 1; i++) {
            LightData light = lights[i];

            bool light_is_directional = light.position.w == 0.0;
            vec3 light_dir_point = normalize(light.position.xyz - P);
            vec3 light_dir_dir = normalize(-light.position.xyz);
            vec3 L = light_is_directional ? light_dir_dir : light_dir_point;

            float dist = length(P - light.position.xyz);
            float attenuation = point_light_attenuation(dist, light.falloff_linear, light.falloff_quadratic);

            vec3 radiance_point = (light.color.rgb * light.color.a) * attenuation;
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
        return;
    }

    // scalar read optimization
    uint hash_and_num_lights = clusters[cluster_id].hash_and_num_lights;
    uint hash = hash_and_num_lights >> 8;
    uint num_lights = hash_and_num_lights & 0xFF;

    if (debug_mode == DEBUG_CLUSTER_SCALAR_READ) {

        // if entire subgroup in tne same cluster, we can read only once
        bool subgroup_in_cluster = subgroupAllEqual(hash);
        if (subgroup_in_cluster) {
            uint first_cluster_item = subgroupBroadcastFirst(cluster_id);
            uint item_count = subgroupBroadcastFirst(num_lights);
            uint last_cluster_item = first_cluster_item + item_count;

            for (uint i = first_cluster_item; i < last_cluster_item; i++) {
                uint cluster_item = cluster_items[i];
                uint light_id = cluster_item;

                LightData light = lights[light_id];

                color += point_light_shade(P, N, V, light, props);
            }
            color += vec3(0, 0.1, 0);
        } else {
            for (uint i = 0; i < num_lights; i++) {
                uint cluster_item = cluster_items[clusters[cluster_id].item_start + i];
                uint light_id = cluster_item;

                LightData light = lights[light_id];

                color += point_light_shade(P, N, V, light, props);
            }
            /*
            // split across multiple clusters. Light using all anyways :D
            uint first_cluster_item = clusters[cluster_id].item_start;
            uint last_cluster_item = first_cluster_item + num_lights;

            // find minimum and maximum cluster item
            uint min_cluster_item = subgroupMin(first_cluster_item);
            uint max_cluster_item = subgroupMax(last_cluster_item);

            for (uint i = min_cluster_item; i < max_cluster_item; i++) {
                uint cluster_item = cluster_items[i];
                uint light_id = cluster_item;

                LightData light = lights[light_id];

                if (i >= first_cluster_item && i < last_cluster_item) {
                    color += point_light_shade(P, N, V, light, props);
                }
            }
            */
        }

        outColor = vec4(color, 1.0);
        return;
    }
    
    // vector read
    {
        for (uint i = 0; i < num_lights; i++) {
            uint cluster_item = cluster_items[clusters[cluster_id].item_start + i];
            uint light_id = cluster_item;

            LightData light = lights[light_id];

            color += point_light_shade(P, N, V, light, props);
        }
    }
    outColor = vec4(color, 1.0);
}
