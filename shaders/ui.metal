#include <metal_stdlib>
using namespace metal;

struct TextVertexInput {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
};

struct TextVertexOut {
    float4 position [[position]];
    float2 uv;
};

struct LineVertexInput {
    float3 position [[attribute(0)]];
};

struct LineVertexOut {
    float4 position [[position]];
};

struct Uniforms {
    float4x4 projection;
};

// Text Shader
vertex TextVertexOut vertex_text(
    TextVertexInput in [[stage_in]],
    constant Uniforms &uniforms [[buffer(0)]]
) {
    TextVertexOut out;
    out.position = uniforms.projection * float4(in.position, 0.0, 1.0);
    out.uv = in.uv;
    return out;
}

fragment float4 fragment_text(
    TextVertexOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler smp [[sampler(0)]],
    constant float4 &color [[buffer(0)]]
) {
    float4 sample = atlas.sample(smp, in.uv);
    return sample * color;
}

// Line Shader
vertex LineVertexOut vertex_line(
    LineVertexInput in [[stage_in]],
    constant Uniforms &uniforms [[buffer(0)]]
) {
    LineVertexOut out;
    out.position = uniforms.projection * float4(in.position, 1.0);
    return out;
}

fragment float4 fragment_line(
    LineVertexOut in [[stage_in]],
    constant float4 &color [[buffer(0)]]
) {
    return color;
}
