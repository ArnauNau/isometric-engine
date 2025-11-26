#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 uv;
};

struct Uniforms {
    float4x4 view_projection;
};

vertex VertexOut vertex_geometry(
    VertexInput in [[stage_in]],
    constant Uniforms &uniforms [[buffer(0)]]
) {
    VertexOut out;
    // Assume z=0 for 2D geometry
    out.position = uniforms.view_projection * float4(in.position, 0.0, 1.0);
    out.color = in.color;
    out.uv = in.uv;
    return out;
}

fragment float4 fragment_geometry(
    VertexOut in [[stage_in]]
) {
    return in.color;
}
