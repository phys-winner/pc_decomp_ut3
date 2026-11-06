// Runtime indexing keeps the shader's literal LOD table in an SM3 constant array.
float4 distance : register(c20);
static const float2 ranges[5] = {
    float2(0, -1), float2(16384, 16384), float2(8192, 8192),
    float2(4096, 4096), float2(0, 4096)
};
float4 vertex(float4 position : POSITION0, float4 terrain : TEXCOORD0,
              out float4 result : TEXCOORD0) : POSITION0
{
    float2 range = ranges[(int)terrain.x];
    float morph = saturate((distance.x - range.x) / range.y);
    result = float4(morph, lerp(terrain.y, terrain.z, morph), range.x / 16384, 1);
    return position;
}
float4 pixel(float4 result : TEXCOORD0) : COLOR0 { return result; }
float4 flat_pixel() : COLOR0 { return 1; }
