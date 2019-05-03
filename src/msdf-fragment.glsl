#version 320 es

precision mediump float;

precision mediump samplerBuffer;
uniform samplerBuffer glyph_metrics;

out vec4 color;

// // enum color {
// const uint BLACK = 0u;
// const uint RED = 1u;
// const uint GREEN = 2u;
// const uint BLUE = 4u;
// const uint YELLOW = RED | GREEN;
// const uint MAGENTA = BLUE | RED;
// const uint CYAN = BLUE | GREEN;
// const uint WHITE = RED | GREEN | BLUE;
// // };

// struct segment {
//     uint color;
//     int npoints;
//     vec2 points[];
// };

// struct contour {
//     int winding;
//     int nsegments;
//     segment segments[];
// };

// struct glyph {
//     int ncontours;
//     contour contours[];
// };

// struct multi_distance {
//     float r;
//     float g;
//     float b;
// };

// /* Local data structures */
// struct distance_selector {
//     vec2 min_true, min_negative, min_positive;
//     float near_param;
//     // segment *near_segment;
// };

// struct segment_selector {
//     distance_selector r, g, b;
// };

// struct workspace {
//     segment_selector shape;
//     segment_selector inner;
//     segment_selector outer;

//     multi_distance max_inner;
//     multi_distance max_outer;
//     multi_distance min_abs;
// };

void main() {
    // workspace ws;
    
    color = vec4(1.0, 1.0, 0.0, 1.0);
    // color = vec4(255.0, 255.0, 255.0, 255.0);
}
