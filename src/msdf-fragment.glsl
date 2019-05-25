#version 320 es

#define IDX_CURR 0
#define IDX_SHAPE 1
#define IDX_INNER 2
#define IDX_OUTER 3
#define IDX_RED 0
#define IDX_GREEN 1
#define IDX_BLUE 2
#define IDX_NEGATIVE 0
#define IDX_POSITIVE 1
#define IDX_MAX_INNER 0
#define IDX_MAX_OUTER 1


precision mediump float;

precision mediump samplerBuffer;
precision mediump usamplerBuffer;
layout (binding = 0) uniform usamplerBuffer metadata;
layout (binding = 1) uniform samplerBuffer point_data;

#define meta_at(i) texelFetch(metadata, int(i)).r
#define point_at(i) vec2(texelFetch(point_data, 2 * int(i)).r, texelFetch(point_data, 2 * int(i) + 1).r)


// #define meta_at(i) mdata[i]
// #define point_at(i) vec2(pdata[2*(i)], pdata[(2*(i))+1])

uniform vec2 offset;

uniform vec2 translate;
uniform vec2 scale;
uniform float range;

out vec4 color;

const float PI = 3.1415926535897932384626433832795;
const float INFINITY = 3.402823466e+38;

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

vec3 signed_distance_linear(vec2 p0, vec2 p1, vec2 origin);
vec3 signed_distance_quad(vec2 p0, vec2 p1, vec2 p2, vec2 origin);
void add_segment_true_distance(int segment_index, int npoints, int points, vec3 d);
vec3 get_pixel_distance(vec2);

vec2 orthonormal(vec2 v) {float len = length(v); return vec2(v.y / len, -v.x / len);}
float cross_(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
float median(vec3 d) {return max(min(d.r, d.g), min(max(d.r, d.g), d.b));}
void add_segment_pseudo_distance(int segment_index, vec2 d);
vec2 distance_to_pseudo_distance(int npoints, int points, vec3 d, vec2 p);
bool point_facing_edge(int prev_npoints, int prev_points, int cur_npoints, int cur_points,
                       int next_npoints, int next_points, vec2 p, float param);
void add_segment(int prev_npoints, int prev_points, int cur_npoints, int cur_points,
                 int next_npoints, int next_points, uint color, vec2 point);
void set_contour_edge(int winding, vec2 point);
float compute_distance(int segment_index, vec2 point);


bool less(vec2 a, vec2 b) {
    return abs(a.x) < abs(b.x) || (abs(a.x) == abs(b.x) && a.y < b.y);
}

float min_distance;
float min_distance_b;

void main() {
    min_distance = 0.0;
    min_distance_b = 0.0;
    // vec2 tr = vec2(0.0, 0.0);

    // color = point_at(0).y > 18.0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);

    // return;
    // vec2 sc = vec2(0.5, 0.5);
    // float r = 4.0;
    // first = true;
    // seg_i = 0;
    vec2 coords = gl_FragCoord.xy - offset;

    vec2 p = ((coords + 0.5) / scale) - translate;
    // vec2 p = vec2((coords.x + 0.5) / scale.x - translate.x,
    //               (coords.y + 0.5) / scale.y - translate.y);
    // p = vec2((coords.x + 0.5), coords.y)
    // p = coords + 0.5;
    // p = gl_FragCoord.xy * 1.0;
    // p.xy = p.yx;
    // p.y = -p.y * 3.0;
    // p = vec2(5.0, 5.0);
    // p.y -= 10.0;
    // p.x -= 5.0;
    // p /= 3.0;
    // p -= 10.0;
    // float min_distance = INFINITY;
    // float min_distance = 0.0;
    // for (int _i_ = 0; _i_ < 24; ++_i_) {
    //     min_distance += (1.0 / (5.0 *distance(p, point_at(_i_))));
    // }
    // // min_distance += (1.0 / (50.0 * distance(p, vec2(5.0, 2.0))));
    // // min_distance += (1.0 / (50.0 * distance(p, vec2(10.0, 7.0))));

    // color = vec4(min_distance, 0.0, 0.0, 1.0);
    // return;
    
    // p *= 0.2;

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
        ws.segments[i].mins[0].y = 0.0;
        ws.segments[i].mins[1].x = -INFINITY;
        ws.segments[i].mins[1].y = 0.0;
        ws.segments[i].min_true.x = -INFINITY;
        ws.segments[i].min_true.y = 0.0;
    }
    int point_index = 0;
    int meta_index = 0;
    

    // uint ncontours = texelFetch(metadata, meta_index++).r;
    uint ncontours = meta_at(meta_index++);
    for (uint _i = 0u; _i < ncontours; ++_i) {
        int winding = int(meta_at(meta_index++)) - 1;
        // int winding = int(texelFetch(metadata, meta_index++).r) - 1;
        // uint nsegments = texelFetch(metadata, meta_index++).r;
        // uint nsegments
        uint nsegments = meta_at(meta_index++);

        uint s_color = meta_at(meta_index);
        uint s_npoints = meta_at(meta_index + 1);
        // uint s_color = texelFetch(metadata, meta_index).r;
        // uint s_npoints = texelFetch(metadata, meta_index + 1).r;

        int cur_points = point_index;
        uint cur_color = meta_at(meta_index + 2 * (int(nsegments) - 1));
        uint cur_npoints = meta_at(meta_index + 2 * (int(nsegments) - 1) + 1);
        // uint cur_color = texelFetch(metadata, meta_index + 2 * int(nsegments - 1u)).r;
        // uint cur_npoints = texelFetch(metadata, meta_index + 2 * int(nsegments - 1u) + 1).r;


        uint prev_npoints = nsegments >= 2u ?
            meta_at(meta_index + 2 * (int(nsegments) - 2) + 1) : s_npoints;
            // texelFetch(metadata, meta_index + 2 * int(nsegments - 2u) + 1).r
            // : s_npoints;
        int prev_points = point_index;

        for (uint _i = 0u; _i < nsegments - 1u; ++_i) {
            uint npoints = meta_at(meta_index + 2 * int(_i) + 1);
            // npoints = 2u;
            // uint npoints = texelFetch(metadata, meta_index + 2 * int(_i) + 1).r;
            cur_points += (int(npoints) - 1);
        }

        for (uint _i = 0u; _i < nsegments - 2u && nsegments >= 2u; ++_i) {
            uint npoints = meta_at(meta_index + 2 * int(_i) + 1);
            // npoints = 2u;
            // uint npoints = texelFetch(metadata, meta_index + 2 * int(_i) + 1).r;
            prev_points += (int(npoints) - 1);
        }

        for (uint _i = 0u; _i < nsegments; ++_i) {
        // for (uint _i = 0u; _i < 1u; ++_i) {
            // cur_points = 5;

            add_segment(int(prev_npoints), prev_points, int(cur_npoints), cur_points,
                        int(s_npoints), point_index, cur_color, p);

            prev_points = cur_points;
            prev_npoints = cur_npoints;
            cur_points = point_index;
            cur_npoints = s_npoints;
            cur_color = s_color;

            // float distance_ = distance(p, point_at(0));
            // distance_ = distance(p, vec2(10.0, 15.0));
            // min_distance = min(min_distance, distance_);

            // s_color = texelFetch(metadata, meta_index++ + 2).r;
            s_color = meta_at(meta_index++ + 2);
            point_index += (int(s_npoints) - 1);
            // s_npoints = texelFetch(metadata, meta_index++ + 2).r;
            s_npoints = meta_at(meta_index++ + 2);
            // s_npoints = 2u;
            // s_npoints = 2u;
            // break;
        }
        point_index += 1;

        set_contour_edge(winding, p);
        // break;
    }

    vec3 d = get_pixel_distance(p);
    
    // for (int _i_ = 0; _i_ < 24; ++_i_) {
    //     min_distance += (1.0 / (10.0 *distance(p, point_at(_i_))));
    // }

    color = vec4(min_distance, min_distance_b, 0.0, 1.0);
    color = vec4(d / range + 0.5, 1.0);
    // float gray = median(color.rgb) > 0.5 ? 1.0 : 0.0;
    // color.r = gray;
    // color.g = gray;
    // color.b = gray;
    // color.rgb = median(color.rgb);
    // color.r = 0.0;
    // color.g = 0.0;
    // color.b = p.y / 20.0;
    // if (coords.y < 3.0)
    //     color.r = 1.0;
    // else
    //     color.r = 0.0;

    // color.r = texelFetch(point_data, point_index - 4).r / 30.0;
}

void merge_segment(int s, int other) {
    if (less(ws.segments[other].min_true.xy, ws.segments[s].min_true.xy)) {
        ws.segments[s].min_true = ws.segments[other].min_true;

        ws.segments[s].nearest_npoints = ws.segments[other].nearest_npoints;
        ws.segments[s].nearest_points = ws.segments[other].nearest_points;
    }
    if (less(ws.segments[other].mins[IDX_NEGATIVE], ws.segments[s].mins[IDX_NEGATIVE]))
        ws.segments[s].mins[IDX_NEGATIVE] = ws.segments[other].mins[IDX_NEGATIVE];
    if (less(ws.segments[other].mins[IDX_POSITIVE], ws.segments[s].mins[IDX_POSITIVE])) {
        ws.segments[s].mins[IDX_POSITIVE] = ws.segments[other].mins[IDX_POSITIVE];
    }
}

void merge_multi_segment(int e, int other) {
    merge_segment(e * 3 + IDX_RED, other * 3 + IDX_RED);
    merge_segment(e * 3 + IDX_GREEN, other * 3 + IDX_GREEN);
    merge_segment(e * 3 + IDX_BLUE, other * 3 + IDX_BLUE);
}

void add_segment(int prev_npoints, int prev_points, int cur_npoints, int cur_points,
                 int next_npoints, int next_points, uint s_color, vec2 point) {

    vec3 d;
    if (cur_npoints == 2)
        d = signed_distance_linear(point_at(cur_points),
                                   point_at(cur_points + 1),
                                   point);
    else
        d = signed_distance_quad(point_at(cur_points),
                                 point_at(cur_points + 1),
                                 point_at(cur_points + 2),
                                 point);


    min_distance += 1.0 / (50.0 * length(d) * length(d));
    min_distance_b += 1.0 / (5.0 * distance(point, point_at(cur_points)));
    min_distance_b += 1.0 / (5.0 * distance(point, point_at(cur_points + 1)));
        // d = signed_distance_linear(texelFetch(point_data, cur_points).rg,
        //                            texelFetch(point_data, (cur_points + 1)).rg,
        //                            point);
        // d = signed_distance_linear(point_data[cur_points], point_data[cur_points + 1],
        //                            point);
    // else
    //     d = signed_distance_quad(texelFetch(point_data, cur_points).rg,
    //                              texelFetch(point_data, (cur_points + 1)).rg,
    //                              texelFetch(point_data, (cur_points + 2)).rg,
    //                              point);
        // d = signed_distance_quad(point_data[cur_points], point_data[cur_points + 1],
        //                          point_data[cur_points + 2], point);
    // if (seg_i++ == 8) {
    // color = vec4(0.0, 0.0, 0.0, 1.0);
    // color.r = abs(d.x) + .5;
    // color.r = 1.0;
    // if (d.r < 0.1 && d.g < 1.0)
    //     color = vec4(1.0, 0.0, 0.0, 1.0);
    // else
    //     color = vec4(0.0, 0.0, 1.0, 1.0);
    // }
    // if (first)
    //     color = vec4(d * -100.0, 1.0);
        // color = vec4(1.0, 0.0, 0.0, 1.0);
    // first = false;

    if ((s_color & RED) > 0u)
        add_segment_true_distance(IDX_CURR * 3 + IDX_RED, cur_npoints, cur_points, d);
    if ((s_color & GREEN) > 0u)
        add_segment_true_distance(IDX_CURR * 3 + IDX_GREEN, cur_npoints, cur_points, d);
    if ((s_color & BLUE) > 0u)
        add_segment_true_distance(IDX_CURR * 3 + IDX_BLUE, cur_npoints, cur_points, d);

    if (point_facing_edge(prev_npoints, prev_points, cur_npoints, cur_points,
                          next_npoints, next_points, point, d.z)) {

        vec2 pd = distance_to_pseudo_distance(cur_npoints, cur_points, d, point);
        // if (s_color & RED)
        if ((s_color & RED) > 0u)
            add_segment_pseudo_distance(IDX_CURR * 3 + IDX_RED, pd);
        // if (s_color & GREEN)
        if ((s_color & GREEN) > 0u)
            add_segment_pseudo_distance(IDX_CURR * 3 + IDX_GREEN, pd);
        // if (s_color & BLUE)
        if ((s_color & BLUE) > 0u)
            add_segment_pseudo_distance(IDX_CURR * 3 + IDX_BLUE, pd);
    }
}

vec3 get_distance(int segment_index, vec2 point) {
    vec3 d;
    d.r = compute_distance(segment_index * 3 + IDX_RED, point);
    d.g = compute_distance(segment_index * 3 + IDX_GREEN, point);
    d.b = compute_distance(segment_index * 3 + IDX_BLUE, point);
    return d;
}

void set_contour_edge(int winding, vec2 point) {

    vec3 d = get_distance(IDX_CURR, point);

    merge_multi_segment(IDX_SHAPE, IDX_CURR);
    if (winding > 0 && median(d) >= 0.0)
        merge_multi_segment(IDX_INNER, IDX_CURR);
    if (winding < 0 && median(d) <= 0.0)
        merge_multi_segment(IDX_INNER, IDX_CURR);

    int i = winding < 0 ? IDX_MAX_INNER : IDX_MAX_OUTER;

    if (median(d) > median(ws.maximums[i]))
        ws.maximums[i] = d;

    if (abs(median(d)) < abs(median(ws.min_absolute)))
        ws.min_absolute = d;
}

vec2 segment_direction(int points, int npoints, float param) {
    return mix(point_at(points + 1) - point_at(points),
               point_at(points + npoints - 1) - point_at(points + npoints - 2),
               param);
}

vec2 segment_point(int points, int npoints, float param) {
    return mix(mix(point_at(points), point_at(points + 1), param),
               mix(point_at(points + npoints - 2), point_at(points + npoints - 1), param),
               param);
}


vec2 distance_to_pseudo_distance(int npoints, int points, vec3 d, vec2 p) {
    if (d.z >= 0.0 && d.z <= 1.0)
        return d.xy;

    vec2 dir = normalize(segment_direction(points, npoints, d.z < 0.0 ? 0.0 : 1.0));
    vec2 aq = p - segment_point(points, npoints, d.z < 0.0 ? 0.0 : 1.0);
    float ts = dot(aq, dir);
    if (d.z < 0.0 ? ts < 0.0 : ts > 0.0) {
        float pseudo_distance = cross_(aq, dir);
        if (abs(pseudo_distance) <= abs(d.x)) {
            d.x = pseudo_distance;
            d.y = 0.0;
        }
    }
    return d.xy;
}

void add_segment_true_distance(int segment_index, int npoints, int points, vec3 d) {
    bool is_less = less(d.xy, ws.segments[segment_index].min_true.xy);
    ws.segments[segment_index].min_true =
        is_less ? d : ws.segments[segment_index].min_true;

    ws.segments[segment_index].nearest_points =
        is_less ? points : ws.segments[segment_index].nearest_points;
    ws.segments[segment_index].nearest_npoints =
        is_less ? npoints : ws.segments[segment_index].nearest_npoints;
}


void add_segment_pseudo_distance(int segment_index, vec2 d) {
    int i = d.x < 0.0 ? IDX_NEGATIVE : IDX_POSITIVE;
    vec2 _d = ws.segments[segment_index].mins[i];
    ws.segments[segment_index].mins[i] = less(d, _d) ? d : _d;
}

bool point_facing_edge(int prev_npoints, int prev_points, int cur_npoints, int cur_points,
                       int next_npoints, int next_points, vec2 p, float param) {

    if (param >= 0.0 && param <= 1.0)
        return true;

    vec2 prev_edge_dir = -normalize(segment_direction(prev_points, prev_npoints, 1.0));
    vec2 edge_dir =
        normalize(segment_direction(cur_points, cur_npoints, param < 0.0 ? 0.0 : 1.0)) *
        (param < 0.0 ? 1.0 : -1.0);
    vec2 next_edge_dir = normalize(segment_direction(next_points, next_npoints, 0.0));
    vec2 point_dir = p - segment_point(cur_points, cur_npoints, param < 0.0 ? 0.0 : 1.0);
    return dot(point_dir, edge_dir) >=
           dot(point_dir, param < 0.0 ? prev_edge_dir : next_edge_dir);
}

float compute_distance(int segment_index, vec2 point) {

    int i = ws.segments[segment_index].min_true.xy.x < 0.0 ? IDX_NEGATIVE : IDX_POSITIVE;
    float min_distance = ws.segments[segment_index].mins[i].x;

    vec2 d = distance_to_pseudo_distance(ws.segments[segment_index].nearest_npoints,
                                         ws.segments[segment_index].nearest_points,
                                         ws.segments[segment_index].min_true, point);
    if (abs(d.x) < abs(min_distance))
        min_distance = d.x;
    return min_distance;
}

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

vec3 get_pixel_distance(vec2 point) {
    vec3 shape_distance = get_distance(IDX_SHAPE, point);
    vec3 inner_distance = get_distance(IDX_INNER, point);
    vec3 outer_distance = get_distance(IDX_OUTER, point);
    float inner_d = median(inner_distance);
    float outer_d = median(outer_distance);
    // color.r = inner_d;
    // color.g = outer_d;
    // color.a = 1.0;

    bool inner = inner_d >= 0.0 && abs(inner_d) <= abs(outer_d);
    bool outer = outer_d <= 0.0 && abs(outer_d) < abs(inner_d);
    if (!inner && !outer)
        return shape_distance;

    vec3 d = inner ? inner_distance : outer_distance;
    vec3 contour_distance = ws.maximums[inner ? IDX_MAX_INNER : IDX_MAX_OUTER];

    float contour_d = median(contour_distance);
    d = (abs(contour_d) < abs(outer_d) && contour_d > median(d)) ? contour_distance : d;

    contour_distance = ws.min_absolute;
    contour_d = median(contour_distance);
    float d_d = median(d);

    d = abs(contour_d) < abs(d_d) ? contour_distance : d;
    d = median(d) == median(shape_distance) ? shape_distance : d;

    return d;
}
