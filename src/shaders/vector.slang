static const uint32_t kNumMaxVectorMeshes = 1024;

struct cbVectorHeapBuffer {
    struct cbVectorObjectData {
        float4x4 world;
        int32_t diffuse_map;
        int32_t reserved0;
        int32_t reserved1;
        int32_t reserved2;
    };

    float4x4 view_projection;
    cbVectorObjectData vector_objects[kNumMaxVectorMeshes];

    uint32_t num_vector_objects;
};

struct VertexShaderInput {
    float3 position;
    float2 uv;
    float4 color;
};

struct VertexShaderOutput {
    float4 position : SV_Position;
    float2 uv;
    float4 color;
};

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("vertex")]
VertexShaderOutput vsMain(VertexShaderInput input, uniform cbVectorHeapBuffer *frame_heap, uniform uint32_t object_id) {
    VertexShaderOutput output;
    float4x4 model_matrix = frame_heap.vector_objects[object_id].world;

    output.position = mul(frame_heap.view_projection, mul(model_matrix, float4(input.position, 1.0)));
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 fsMain(VertexShaderOutput input, uniform cbVectorHeapBuffer *frame_heap, uniform uint32_t object_id) {
    int32_t diffuse_map = frame_heap.vector_objects[object_id].diffuse_map;

    if (diffuse_map >= 0) {
        return float4(textures[diffuse_map].Sample(input.uv).rgb, 1.0);
    } else {
        return input.color;
    }
}
