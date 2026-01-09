#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct Uniforms {
    float4x4 projection;
};

vertex VertexOut vertex_nuklear(
    VertexInput in [[stage_in]],
    constant Uniforms &uniforms [[buffer(0)]]
) {
    VertexOut out;
    out.position = uniforms.projection * float4(in.position, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

fragment float4 fragment_nuklear(
    VertexOut in [[stage_in]],
    texture2d<float> tex [[texture(0)]],
    sampler smp [[sampler(0)]]
) {
    return tex.sample(smp, in.uv) * in.color;
}
