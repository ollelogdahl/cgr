#define MAX_CLUSTER_ITEMS 256

struct ClusterData {
    vec4 bounds_min;
    vec4 bounds_max;
    uint item_start;
    uint num_lights;
};
struct ClusterConstants {
    float znear;
    float zfar;
    uint grid_x;
    uint grid_y;
    uint grid_z;
};

bool cluster_index_valid(uint cluster_index, ClusterConstants constants) {
    return cluster_index < constants.grid_x * constants.grid_y * constants.grid_z;
}

uvec3 cluster_calc_pos(uint cluster_index, ClusterConstants constants) {
    return uvec3(
        cluster_index % constants.grid_x,
        (cluster_index / constants.grid_x) % constants.grid_y,
        cluster_index / (constants.grid_x * constants.grid_y)
    );
}

uint cluster_lookup(vec3 clip_pos, vec3 view_pos, ClusterConstants constants) {
    // @todo: do we need both clip and view pos?
    uint ztile = uint(
        log(abs(view_pos.z) / constants.znear)
        / log(constants.zfar / constants.znear)
        * float(constants.grid_z)
    );

    vec2 grid_xy = vec2(constants.grid_x, constants.grid_y);
    uvec2 tile_xy = uvec2(
        (clip_pos.xy + 1.0) * 0.5 * grid_xy
    );

    uvec3 tile = uvec3(tile_xy, ztile);
    uint cluster_id = tile.x
        + tile.y * constants.grid_x
        + tile.z * constants.grid_x * constants.grid_y;

    return cluster_id;
}
