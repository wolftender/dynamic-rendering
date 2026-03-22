struct cbLightingPassConstants {
    uint32_t texture;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct VertexShaderOutput {
    float4 position : SV_Position;
    float2 uv;
};

[[vk::binding(0, 0)]]
Sampler2D textures[];

[shader("vertex")]
VertexShaderOutput vsMain(uint32_t vertexIndex: SV_VertexID, uniform cbLightingPassConstants constants) {
    VertexShaderOutput output;
    output.uv = float2((vertexIndex << 1) & 2, vertexIndex & 2);
    output.position = float4(output.uv * 2.0f + -1.0f, 0.0f, 1.0f);

    return output;
}

[shader("fragment")]
float4 fsMain(VertexShaderOutput input, uniform cbLightingPassConstants constants) {
    return float4(textures[constants.texture].Sample(input.uv).rgba);
}
