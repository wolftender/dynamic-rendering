const static uint32_t kNumMaxPointLights = 20;
const static uint32_t kNumMaxStaticObjects = 1024;
const static uint32_t kNumMaxSkinnedObjects = 128;
const static uint32_t kNumMaxBonesPerObject = 200;
const static uint32_t kNumTexturePoolSize = 256;

struct cbFrameHeapBuffer {
    struct cbPointLightData {
        float4 world_position;
        float4 color_intensity;
    };

    struct cbStaticObjectData {
        float4x4 world;
        int32_t diffuse_map;
        int32_t normal_map;
        int32_t reserved0;
        int32_t reserved1;
    };

    float4x4 projection;
    float4x4 view;

    float4x4 projection_inv;
    float4x4 view_inv;

    float4 ambient;

    cbPointLightData point_lights[kNumMaxPointLights];
    cbStaticObjectData static_objects[kNumMaxStaticObjects];

    uint32_t num_static_objects;
    uint32_t num_lights;
};

struct VertexShaderInput {
    float3 position;
    float3 normal;
    float2 uv;
    float3 color;
    float4 tangent;
};

Sampler2D u_textures[];

struct VertexShaderOutput {
    float4 position : SV_Position;
    float3 normal;
    float2 uv;
    float3 color;
    float4 tangent;
};

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("vertex")]
VertexShaderOutput vsMain(VertexShaderInput input, uniform cbFrameHeapBuffer *frame_heap, uniform uint32_t object_id) {
    VertexShaderOutput output;
    float4x4 model_matrix = frame_heap.static_objects[object_id].world;

    output.position = mul(frame_heap.projection, mul(frame_heap.view, mul(model_matrix, float4(input.position, 1.0))));
    output.normal = input.normal;
    output.uv = input.uv;
    output.color = input.color;
    output.tangent = input.tangent;

    return output;
}

[shader("fragment")]
float4 fsMain(VertexShaderOutput input, uniform cbFrameHeapBuffer *frame_heap, uniform uint32_t object_id) {
    int32_t diffuse_map = frame_heap.static_objects[object_id].diffuse_map;

    if (diffuse_map >= 0) {
        return float4(textures[diffuse_map].Sample(input.uv).rgb, 1.0);
    } else {
        return float4(input.normal, 1.0);
    }
}
