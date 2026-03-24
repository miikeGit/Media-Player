struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

VS_OUTPUT main(uint id : SV_VertexID) {
    VS_OUTPUT output;
    output.texCoord = float2(id & 1, id >> 1);
    output.pos = float4(output.texCoord.x * 2.0 - 1.0, -(output.texCoord.y * 2.0 - 1.0), 0, 1);
    return output;
}