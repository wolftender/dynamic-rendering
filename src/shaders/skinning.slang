struct SkinnedVertex {
    float4 position; // vec3 + pad
    float4 normal;   // vec3 + pad
    float4 uv;       // vec2 + pad
    float4 color;    // vec3 + pad
    float4 tangent;
    uint4 bones;
    float4 weights;
}

struct StaticVertex {
    float4 position; // vec3 + pad
    float4 normal;   // vec3 + pad
    float4 uv;       // vec2 + pad
    float4 color;    // vec3 + pad
    float4 tangent;
}

static const uint32_t kNumMaxBonesPerObject = 200;

struct BoneMatrixBuffer {
    float4x4 inverse_bind[kNumMaxBonesPerObject];
}

[shader("compute")]
[numthreads(64, 1, 1)]
void csMain(
    uint3 thread_id: SV_DispatchThreadID, uniform BoneMatrixBuffer *bones, uniform SkinnedVertex *input,
    uniform StaticVertex *output) {
    uint index = thread_id.x;

    uint b0 = input[index].bones.x;
    uint b1 = input[index].bones.y;
    uint b2 = input[index].bones.z;
    uint b3 = input[index].bones.w;

    float w0 = input[index].weights.x;
    float w1 = input[index].weights.y;
    float w2 = input[index].weights.z;
    float w3 = input[index].weights.w;

    float4x4 T = bones->inverse_bind[b0] * w0 + bones->inverse_bind[b1] * w1 + bones->inverse_bind[b2] * w2 +
                 bones->inverse_bind[b3] * w3;

    output[index].position = mul(T, float4(input[index].position.xyz, 1.0f));
}
