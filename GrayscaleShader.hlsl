Texture2D tex : register(t0);
SamplerState samp : register(s0);

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 texCoord : TEXCOORD;
};

float4 main(VS_OUTPUT input) : SV_TARGET {
    float4 color = tex.Sample(samp, input.texCoord);
    float gray = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    return float4(gray, gray, gray, color.a);
}