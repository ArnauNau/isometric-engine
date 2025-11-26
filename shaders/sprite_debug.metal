#include <metal_stdlib>
using namespace metal;

// Must match C struct SpriteInstance exactly:
// typedef struct {
//   float x, y, z, p1;    // 16 bytes
//   float w, h, p2, p3;   // 16 bytes
//   float u, v, uw, vh;   // 16 bytes
// } SpriteInstance;
struct InstanceData {
    float4 position;      // x, y, z, padding (16 bytes) - use .xyz
    float4 size;          // w, h, padding, padding (16 bytes) - use .xy
    float4 texRegion;     // u, v, uw, vh (16 bytes)
};

struct Uniforms {
    float4x4 viewProjection;
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

// SDL_GPU on Metal might order buffers as:
// - Uniform buffers first: buffer(0)
// - Storage buffers after: buffer(1)
vertex VertexOut vertex_main(
    uint vertexID [[vertex_id]],
    uint instanceID [[instance_id]],
    constant Uniforms &uniforms [[buffer(0)]],
    const device InstanceData *instances [[buffer(1)]]
) {
    InstanceData instance = instances[instanceID];

    // Standard quad vertices (0,0 to 1,1)
    float2 quadVerts[6] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(0.0, 1.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0)
    };

    float2 vertPos = quadVerts[vertexID];

    // Scale position by instance size
    float2 worldPos = instance.position.xy + (vertPos * instance.size.xy);

    // Transform to clip space
    float4 pos = uniforms.viewProjection * float4(worldPos, instance.position.z, 1.0);

    // Calculate texture coordinates
    float2 texCoord = instance.texRegion.xy + (vertPos * instance.texRegion.zw);

    VertexOut out;
    out.position = pos;
    out.texCoord = texCoord;
    return out;
}

fragment float4 fragment_main(
    VertexOut in [[stage_in]],
    texture2d<float> spriteTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]]
) {
    // DEBUG: Output magenta to verify pipeline works
//    return float4(1.0, 0.0, 1.0, 1.0);

    // Uncomment below for actual texture sampling:
     float4 color = spriteTexture.sample(textureSampler, in.texCoord);
     if (color.a < 0.1) {
         discard_fragment();
     }
     return color;
}
