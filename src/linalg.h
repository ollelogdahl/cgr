#pragma once

#include <fmt/core.h>

#include <math.h>
#include <immintrin.h>

#include "oc.h"

// represents an angle. internally stored as radians.
struct anglef {
    f32 value;

    constexpr anglef static inline zero() {
        return {0};
    }

    constexpr f32 operator()() const {
        return value;
    }

    constexpr anglef static inline from_rad(f32 value) {
        return {value};
    }
    constexpr anglef static inline from_deg(f32 value) {
        if (value > 360) value -= 360;
        if (value < -360) value += 360;

        return {(f32)(value * M_PI / 180)};
        // get the value in 0..360 range
    }

    constexpr f32 inline as_rad() const {
        return value;
    }

    constexpr f32 inline as_deg() const {
        return value * 180 / M_PI;
    }

    constexpr anglef operator+(anglef other) const {
        return {value + other.value};
    }

    constexpr anglef operator-(anglef other) const {
        return {value - other.value};
    }

    constexpr anglef operator*(f32 scalar) const {
        return {value * scalar};
    }

    constexpr anglef operator/(f32 scalar) const {
        return {value / scalar};
    }

    constexpr anglef& operator+=(anglef other) {
        value += other.value;
        return *this;
    }
    constexpr anglef& operator-=(anglef other) {
        value -= other.value;
        return *this;
    }

    constexpr inline bool operator<(const anglef &other) const {
        return value < other.value;
    }
    constexpr inline bool operator>(const anglef &other) const {
        return value > other.value;
    }
private:
    constexpr anglef(f32 value) : value(value) {}
};

struct v2f;
struct v3f;
struct v4f;
struct m4f;

struct v2f {
    f32 x, y;

    constexpr v2f &operator+=(v2f other) {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr v2f &operator-=(v2f other) {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr v2f &operator*=(f32 scalar) {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr v2f operator+(v2f other) const {
        return {x + other.x, y + other.y};
    }

    constexpr v2f operator-(v2f other) const {
        return {x - other.x, y - other.y};
    }

    constexpr v2f operator*(f32 scalar) const {
        return {x * scalar, y * scalar};
    }

    f32 constexpr static dot(v2f a, v2f b) {
        return a.x * b.x + a.y * b.y;
    }

    f32 constexpr length() const {
        return sqrt(x * x + y * y);
    }

    v2f &normalized() {
        f32 length = sqrt(x * x + y * y);
        x /= length;
        y /= length;
        return *this;
    }

    v2f constexpr static normalize(v2f v) {
        f32 length = sqrt(v.x * v.x + v.y * v.y);
        return {v.x / length, v.y / length};
    }
};

struct v3f {
    f32 x, y, z;

    constexpr v3f &operator+=(v3f other) {
        x += other.x;
        y += other.y;
        z += other.z;
        return *this;
    }

    constexpr v3f &operator-=(v3f other) {
        x -= other.x;
        y -= other.y;
        z -= other.z;
        return *this;
    }

    constexpr v3f &operator*=(f32 scalar) {
        x *= scalar;
        y *= scalar;
        z *= scalar;
        return *this;
    }

    v3f constexpr operator+(v3f other) const {
        return {x + other.x, y + other.y, z + other.z};
    }

    v3f constexpr operator-(v3f other) const {
        return {x - other.x, y - other.y, z - other.z};
    }

    v3f constexpr operator*(f32 scalar) const {
        return {x * scalar, y * scalar, z * scalar};
    }

    f32 constexpr static dot(v3f a, v3f b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    v3f constexpr static cross(v3f a, v3f b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    f32 constexpr length() const {
        return sqrt(x * x + y * y + z * z);
    }

    f32 constexpr squared_mag() const {
        return x * x + y * y + z * z;
    }

    f32 constexpr square_distance(v3f other) const {
        return (x - other.x) * (x - other.x) + (y - other.y) * (y - other.y) + (z - other.z) * (z - other.z);
    }

    v3f constexpr static normalize(v3f v) {
        f32 length = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        return {v.x / length, v.y / length, v.z / length};
    }

    v3f constexpr normalized() const {
        f32 length = sqrt(x * x + y * y + z * z);
        return {x / length, y / length, z / length};
    }

    v4f constexpr to_homogeneous() const;
};

struct v4f {
    f32 x, y, z, w;

    v4f constexpr operator*(const m4f &matrix) const;

    v3f constexpr perspective_divide() const {
        return {x / w, y / w, z / w};
    }

    v3f constexpr xyz() const {
        return {x, y, z};
    }
};

v4f constexpr v3f::to_homogeneous() const {
    return {x, y, z, 1};
}

// note: matrices are stored as column-major.
struct m4f {
    f32 m[16];

    // constructor taking the elements in column-major order
    static constexpr m4f colmaj(f32 a, f32 b, f32 c, f32 d,
                                f32 e, f32 f, f32 g, f32 h,
                                f32 i, f32 j, f32 k, f32 l,
                                f32 m, f32 n, f32 o, f32 p) {
        return {
            a, b, c, d,
            e, f, g, h,
            i, j, k, l,
            m, n, o, p
        };
    }

    // a constructor taking the elements in row-major order
    static constexpr m4f rowmaj(f32 a, f32 b, f32 c, f32 d,
                                f32 e, f32 f, f32 g, f32 h,
                                f32 i, f32 j, f32 k, f32 l,
                                f32 m, f32 n, f32 o, f32 p) {
        return {
            a, e, i, m,
            b, f, j, n,
            c, g, k, o,
            d, h, l, p
        };
    }

    static constexpr m4f identity() {
        return {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1,
        };
    }

    m4f constexpr operator *(const m4f &other) const;

    m4f static constexpr translate(v3f translation) {
        return {{
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            translation.x, translation.y, translation.z, 1,
        }};
    }

    m4f static constexpr scale(v3f scale) {
        return {{
            scale.x, 0, 0, 0,
            0, scale.y, 0, 0,
            0, 0, scale.z, 0,
            0, 0, 0, 1,
        }};
    }

    m4f static constexpr rotate(anglef angle, v3f axis) {
        f32 c = cos(angle.as_rad());
        f32 s = sin(angle.as_rad());
        f32 t = 1 - c;
        v3f n = v3f::normalize(axis);
        f32 x = n.x;
        f32 y = n.y;
        f32 z = n.z;

        return {{
            t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0,
            t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0,
            t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0,
            0, 0, 0, 1,
        }};
    }

    m4f static constexpr perspective(anglef fov, f32 aspect, f32 near, f32 far);
    m4f static constexpr orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far);
    m4f static constexpr look_at(v3f eye, v3f center, v3f up);

    // reverse z: [0, 1] to [1, 0]
    m4f static constexpr reverse_z(const m4f &mat);

    // map the z-range from [-1, 1] to [0, 1]
    m4f static constexpr normalize_unit_range(const m4f &mat);

    m4f static constexpr inverse(m4f &mat);
};

// a 4x4 matrix with an inverse precomputed
struct m4fbi {
    m4f m;
    m4f inv;
};

struct aabb_t {
    v3f min, max;

    constexpr aabb_t() : min({F32_MAX, F32_MAX, F32_MAX}), max({-F32_MAX, -F32_MAX, -F32_MAX}) {}
    constexpr aabb_t(v3f min, v3f max) : min(min), max(max) {}

    constexpr static aabb_t from_center(v3f center, v3f size) {
        return {center - size * 0.5f, center + size * 0.5f};
    }

    void include(v3f p) {
        min.x = fmin(min.x, p.x);
        min.y = fmin(min.y, p.y);
        min.z = fmin(min.z, p.z);
        max.x = fmax(max.x, p.x);
        max.y = fmax(max.y, p.y);
        max.z = fmax(max.z, p.z);
    }

    void include(aabb_t other) {
        include(other.min);
        include(other.max);
    }

    aabb_t transform_affine(const m4f &matrix) const {
        aabb_t result;

        // Transform all 8 corners
        v3f corners[8] = {
            {min.x, min.y, min.z}, // 000
            {max.x, min.y, min.z}, // 100
            {min.x, max.y, min.z}, // 010
            {max.x, max.y, min.z}, // 110
            {min.x, min.y, max.z}, // 001
            {max.x, min.y, max.z}, // 101
            {min.x, max.y, max.z}, // 011
            {max.x, max.y, max.z}  // 111
        };

        // Transform each corner and include it in the result
        for (int i = 0; i < 8; i++) {
            v3f transformed = (corners[i].to_homogeneous() * matrix).xyz();
            result.include(transformed);
        }

        return result;
    }

    v3f center() const {
        return (min + max) * 0.5f;
    }

    v3f size() const {
        return max - min;
    }
};

m4f constexpr m4f::operator*(const m4f &o) const {
    // This usually gets auto-vectorized by the compiler.
    m4f res = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                res.m[i * 4 + j] += m[i * 4 + k] * o.m[k * 4 + j];
            }
        }
    }
    return res;
}

v4f constexpr v4f::operator*(const m4f &matrix) const {
    return {
        x * matrix.m[0] + y * matrix.m[4] + z * matrix.m[8] + w * matrix.m[12],
        x * matrix.m[1] + y * matrix.m[5] + z * matrix.m[9] + w * matrix.m[13],
        x * matrix.m[2] + y * matrix.m[6] + z * matrix.m[10] + w * matrix.m[14],
        x * matrix.m[3] + y * matrix.m[7] + z * matrix.m[11] + w * matrix.m[15]
    };
}

// left-handed perspective projection
m4f constexpr m4f::perspective(anglef fov, f32 aspect, f32 near, f32 far) {
    float f = 1.0f / tanf(fov.as_rad() / 2);

    float A = (far + near) / (near - far);
    float B = (2 * far * near) / (near - far);

    return m4f::colmaj(
        f / aspect, 0, 0, 0,
        0, -f, 0, 0,
        0, 0, A, -1,
        0, 0, B, 0
    );
}

m4f constexpr m4f::orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 near, f32 far) {
    return m4f{
        2 / (right - left), 0, 0, 0,
        0, 2 / (top - bottom), 0, 0,
        0, 0, -2 / (far - near), 0,
        -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1
    };
}

m4f constexpr m4f::look_at(v3f eye, v3f center, v3f up) {
    v3f f = (center - eye).normalized();
    v3f s = v3f::cross(f, up).normalized();
    v3f u = v3f::cross(s, f);

    return m4f{
        s.x, u.x, -f.x, 0,
        s.y, u.y, -f.y, 0,
        s.z, u.z, -f.z, 0,
        -v3f::dot(s, eye), -v3f::dot(u, eye), v3f::dot(f, eye), 1
    };
}

m4f constexpr m4f::reverse_z(const m4f &mat) {
    m4f reverse = m4f::colmaj(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, -1., 0.0,
        0.0, 0.0, 1.0, 1.0
    );
    return mat * reverse;
}

m4f constexpr m4f::normalize_unit_range(const m4f &mat) {
    m4f normalize = m4f::colmaj(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 0.5, 0.0,
        0.0, 0.0, 0.5, 1.0
    );
    return mat * normalize;
}

m4f constexpr m4f::inverse(m4f &mat) {
    // calculate the inverse of a 4x4 matrix
    m4f inverse;
    f32 *m = mat.m;
    f32 *inv = inverse.m;

    // taken from mesa glu library
    inv[0] = m[5]  * m[10] * m[15] -
                m[5]  * m[11] * m[14] -
                m[9]  * m[6]  * m[15] +
                m[9]  * m[7]  * m[14] +
                m[13] * m[6]  * m[11] -
                m[13] * m[7]  * m[10];
    inv[4] = -m[4]  * m[10] * m[15] +
                m[4]  * m[11] * m[14] +
                m[8]  * m[6]  * m[15] -
                m[8]  * m[7]  * m[14] -
                m[12] * m[6]  * m[11] +
                m[12] * m[7]  * m[10];
    inv[8] = m[4]  * m[9] * m[15] -
                m[4]  * m[11] * m[13] -
                m[8]  * m[5] * m[15] +
                m[8]  * m[7] * m[13] +
                m[12] * m[5] * m[11] -
                m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] +
                m[4]  * m[10] * m[13] +
                m[8]  * m[5] * m[14] -
                m[8]  * m[6] * m[13] -
                m[12] * m[5] * m[10] +
                m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] +
                m[1]  * m[11] * m[14] +
                m[9]  * m[2] * m[15] -
                m[9]  * m[3] * m[14] -
                m[13] * m[2] * m[11] +
                m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] -
                m[0]  * m[11] * m[14] -
                m[8]  * m[2] * m[15] +
                m[8]  * m[3] * m[14] +
                m[12] * m[2] * m[11] -
                m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] +
                m[0]  * m[11] * m[13] +
                m[8]  * m[1] * m[15] -
                m[8]  * m[3] * m[13] -
                m[12] * m[1] * m[11] +
                m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] -
                m[0]  * m[10] * m[13] -
                m[8]  * m[1] * m[14] +
                m[8]  * m[2] * m[13] +
                m[12] * m[1] * m[10] -
                m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] -
                m[1]  * m[7] * m[14] -
                m[5]  * m[2] * m[15] +
                m[5]  * m[3] * m[14] +
                m[13] * m[2] * m[7] -
                m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] +
                m[0]  * m[7] * m[14] +
                m[4]  * m[2] * m[15] -
                m[4]  * m[3] * m[14] -
                m[12] * m[2] * m[7] +
                m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] -
                m[0]  * m[7] * m[13] -
                m[4]  * m[1] * m[15] +
                m[4]  * m[3] * m[13] +
                m[12] * m[1] * m[7] -
                m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] +
                m[0]  * m[6] * m[13] +
                m[4]  * m[1] * m[14] -
                m[4]  * m[2] * m[13] -
                m[12] * m[1] * m[6] +
                m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] +
                m[1] * m[7] * m[10] +
                m[5] * m[2] * m[11] -
                m[5] * m[3] * m[10] -
                m[9] * m[2] * m[7] +
                m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] -
                m[0] * m[7] * m[10] -
                m[4] * m[2] * m[11] +
                m[4] * m[3] * m[10] +
                m[8] * m[2] * m[7] -
                m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] +
                m[0] * m[7] * m[9] +
                m[4] * m[1] * m[11] -
                m[4] * m[3] * m[9] -
                m[8] * m[1] * m[7] +
                m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] -
                m[0] * m[6] * m[9] -
                m[4] * m[1] * m[10] +
                m[4] * m[2] * m[9] +
                m[8] * m[1] * m[6] -
                m[8] * m[2] * m[5];

    f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    // assert(det != 0);

    det = 1.0f / det;
    for (u32 i = 0; i < 16; i++) {
        inv[i] *= det;
    }
    return inverse;
}

struct transform_t {
    v3f position = {0, 0, 0};
    v3f scale = {1, 1, 1};
    v3f rotation = {0, 0, 0};

    m4f matrix() const {
        m4f rot = m4f::rotate(anglef::from_deg(rotation.x), {1, 0, 0})
            * m4f::rotate(anglef::from_deg(rotation.y), {0, 1, 0})
            * m4f::rotate(anglef::from_deg(rotation.z), {0, 0, 1});
        return m4f::scale(scale) * rot * m4f::translate(position);
    }
};

template <>
struct fmt::formatter<anglef> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const anglef& angle, auto& ctx) const {
        return format_to(ctx.out(), "{}°", angle.as_deg());
    }
};

template <>
struct fmt::formatter<v2f> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const v2f& v, auto& ctx) const {
        return format_to(ctx.out(), "({}, {})", v.x, v.y);
    }
};

template <>
struct fmt::formatter<aabb_t> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const aabb_t& aabb, auto& ctx) const {
        return format_to(ctx.out(), "min: {}, max: {}", aabb.min, aabb.max);
    }
};

template <>
struct fmt::formatter<v3f> {
    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}')
            ++it;
        return it;
    }

    auto format(const v3f& v, auto& ctx) const {
        return format_to(ctx.out(), "({}, {}, {})", v.x, v.y, v.z);
    }
};

template <>
struct fmt::formatter<v4f> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const v4f& v, auto& ctx) const {
        return format_to(ctx.out(), "({}, {}, {}, {})", v.x, v.y, v.z, v.w);
    }
};

template <>
struct fmt::formatter<m4f> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const m4f& m, auto& ctx) const {
        return format_to(ctx.out(), "[[{}, {}, {}, {}], [{}, {}, {}, {}], [{}, {}, {}, {}], [{}, {}, {}, {}]]",
                         m.m[0], m.m[1], m.m[2], m.m[3],
                         m.m[4], m.m[5], m.m[6], m.m[7],
                         m.m[8], m.m[9], m.m[10], m.m[11],
                         m.m[12], m.m[13], m.m[14], m.m[15]);
    }
};
