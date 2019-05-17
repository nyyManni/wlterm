#version 320 es

precision mediump float;

precision mediump samplerBuffer;
precision mediump usamplerBuffer;
layout (binding = 2) uniform usamplerBuffer metadata;
layout (binding = 3) uniform samplerBuffer point_data;

uniform vec2 offset;

uniform vec2 translate;
uniform vec2 scale;
uniform float range;

out vec4 color;

const float PI = 3.1415926535897932384626433832795;
// const float INFINITY = 3.402823466e+38;
const float INFINITY = 3.402823466e+32;

const uint BLACK = 0u;
const uint RED = 1u;
const uint GREEN = 2u;
const uint BLUE = 4u;
const uint YELLOW = RED | GREEN;
const uint MAGENTA = BLUE | RED;
const uint CYAN = BLUE | GREEN;
const uint WHITE = RED | GREEN | BLUE;

struct segment {
    vec3 min_true;
    vec2 mins[2];
    int nearest_points;
    int nearest_npoints;
};

struct workspace {
    segment segments[4 * 3];

    vec3 maximums[2];
    vec3 min_absolute;
};

workspace ws;

vec2 orthonormal(vec2 v) {float len = length(v); return vec2(v.y / len, -v.x / len);}
float cross_(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
float median(vec3 d) {return max(min(d.r, d.g), min(max(d.r, d.g), d.b));}

void main() {
    vec2 coords = gl_FragCoord.xy - offset;
    vec2 p = vec2((coords.x + 0.5) / scale.x - translate.x,
                  (coords.y + 0.5) / scale.y - translate.y);

    ws.maximums[0].r = -INFINITY;
    ws.maximums[1].r = -INFINITY;
    ws.maximums[0].g = -INFINITY;
    ws.maximums[1].g = -INFINITY;
    ws.maximums[0].b = -INFINITY;
    ws.maximums[1].b = -INFINITY;
    ws.min_absolute.r = -INFINITY;
    ws.min_absolute.g = -INFINITY;
    ws.min_absolute.b = -INFINITY;

    for (int i = 0; i < (4 * 3); ++i) {
        ws.segments[i].mins[0].x = -INFINITY;
        ws.segments[i].mins[1].x = -INFINITY;
        ws.segments[i].min_true.x = -INFINITY;
    }
    int point_index = 0;
    int meta_index = 0;

    uint ncontours = texelFetch(metadata, meta_index++).r;
    for (uint _i = 0u; _i < ncontours; ++_i) {
        int winding = int(texelFetch(metadata, meta_index++).r) - 1;
        uint nsegments = texelFetch(metadata, meta_index++).r;

        uint s_color = texelFetch(metadata, meta_index).r;
        uint s_npoints = texelFetch(metadata, meta_index + 1).r;

        int cur_points = point_index;
        uint cur_color = texelFetch(metadata, meta_index + 2 * int(nsegments - 1u)).r;
        uint cur_npoints = texelFetch(metadata, meta_index + 2 * int(nsegments - 1u) + 1).r;

        uint prev_npoints = nsegments >= 2u ?
            texelFetch(metadata, meta_index + 2 * int(nsegments - 2u) + 1).r
            : s_npoints;
        int prev_points = point_index;

        for (uint _i = 0u; _i < nsegments - 1u; ++_i) {
            uint npoints = texelFetch(metadata, meta_index + 2 * int(_i) + 1).r;
            cur_points += int(npoints) - 1;
        }

        for (uint _i = 0u; _i < nsegments - 2u && nsegments >= 2u; ++_i) {
            uint npoints = texelFetch(metadata, meta_index + 2 * int(_i) + 1).r;
            prev_points += int(npoints) - 1;
        }

        for (uint _i = 0u; _i < nsegments; ++_i) {

            // add_segment

            prev_points = cur_points;
            prev_npoints = cur_npoints;
            cur_points = point_index;
            cur_npoints = s_npoints;
            cur_color = s_color;

            s_color = texelFetch(metadata, meta_index++ + 2).r;
            point_index += int(s_npoints) - 1;
            s_npoints = texelFetch(metadata, meta_index++ + 2).r;
        }
        point_index += 1;

        // set_contour_edge
    }

    // get_pixel_distance

    float gray = 0.5;
    color = vec4(gray, gray, gray, 1.0);
}

// float compute_distance(int segment_index, vec2 point) {

//     int i = ws.segments[segment_index].min_true.xy.x < 0 ? IDX_NEGATIVE : IDX_POSITIVE;
//     float min_distance = ws.segments[segment_index].mins[i].x;

//     vec2 d = distance_to_pseudo_distance(ws.segments[segment_index].nearest_npoints,
//                                          ws.segments[segment_index].nearest_points,
//                                          ws.segments[segment_index].min_true, point);
//     if (fabs(d.x) < fabs(min_distance))
//         min_distance = d.x;
//     return min_distance;
// }

vec3 signed_distance_linear(vec2 p0, vec2 p1, vec2 origin) {
    vec2 aq = origin - p0;
    vec2 ab = p1 - p0;
    float param = dot(aq, ab) / dot(ab, ab);
    vec2 eq = (param > .5 ? p1 : p0) - origin;
    float endpoint_distance = length(eq);
    if (param > 0.0 && param < 1.0) {
        float ortho_distance = dot(orthonormal(ab), aq);
        if (abs(ortho_distance) < endpoint_distance)
            return vec3(ortho_distance, 0, param);
    }
    return vec3(sign(cross_(aq, ab)) *endpoint_distance,
                abs(dot(normalize(ab), normalize(eq))),
                param);
}

vec3 signed_distance_quad(vec2 p0, vec2 p1, vec2 p2, vec2 origin) {
    vec2 qa = p0 - origin;
    vec2 ab = p1 - p0;
    vec2 br = p2 - p1 - ab;
    float a = dot(br, br);
    float b = 3.0 * dot(ab, br);
    float c = 2.0 * dot(ab, ab) + dot(qa, br);
    float d = dot(qa, ab);
    float coeffs[3];
    float _a = b / a;
    int solutions;

    float a2 = _a * _a;
    float q = (a2 - 3.0 * (c / a)) / 9.0;
    float r = (_a * (2.0 * a2 - 9.0 * (c / a)) + 27.0 * (d / a)) / 54.0;
    float r2 = r * r;
    float q3 = q * q * q;
    float A, B;
    _a /= 3.0;
    float t = r / sqrt(q3);
    t = t < -1.0 ? -1.0 : t;
    t = t > 1.0 ? 1.0 : t;
    t = acos(t);
    A = -pow(abs(r) + sqrt(r2 - q3), 1.0 / 3.0);
    A = r < 0.0 ? -A : A;
    B = A == 0.0 ? 0.0 : q / A;
    if (r2 < q3) {
        q = -2.0 * sqrt(q);
        coeffs[0] = q * cos(t / 3.0) - _a;
        coeffs[1] = q * cos((t + 2.0 * PI) / 3.0) - _a;
        coeffs[2] = q * cos((t - 2.0 * PI) / 3.0) - _a;
        solutions = 3;
    } else {
        coeffs[0] = (A + B) - _a;
        coeffs[1] = -0.5 * (A + B) - _a;
        coeffs[2] = 0.5 * sqrt(3.0) * (A - B);
        solutions = abs(coeffs[2]) < 1.0e-14 ? 2 : 1;
    }

    float min_distance = sign(cross_(ab, qa)) * length(qa); // distance from A
    float param = -dot(qa, ab) / dot(ab, ab);
    float distance = sign(cross_(p2 - p1, p2 - origin)) * length(p2 - origin); // distance from B
    if (abs(distance) < abs(min_distance)) {
        min_distance = distance;
        param = dot(origin - p1, p2 - p1) / dot(p2 - p1, p2 - p1);
    }
    for (int i = 0; i < solutions; ++i) {
        if (coeffs[i] > 0.0 && coeffs[i] < 1.0) {
            vec2 endpoint = p0 + ab * 2.0 * coeffs[i] + br * coeffs[i] * coeffs[i];
            float distance = sign(cross_(p2 - p0, endpoint - origin)) * length(endpoint - origin);
            if (abs(distance) <= abs(min_distance)) {
                min_distance = distance;
                param = coeffs[i];
            }
        }
    }
    vec2 v = vec2(min_distance, 0.0);
    v.y = param > 1.0 ? abs(dot(normalize(p2 - p1), normalize(p2 - origin))) : v.y;
    v.y = param < 0.0 ? abs(dot(normalize(ab), normalize(qa))) : v.y;

    return vec3(v, param);
}
