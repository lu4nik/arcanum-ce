cbuffer LitUniforms : register(b0, space3)
{
    int4 _51_u_rect : packoffset(c0);
    float4 _51_u_sundir : packoffset(c1);
    float4 _51_u_grade : packoffset(c2);
    float4 _51_u_params : packoffset(c3);
    int4 _51_u_iparams : packoffset(c4);
    float4 _51_u_hd : packoffset(c5);
    float4 _51_u_lpos[64] : packoffset(c6);
    float4 _51_u_lcol[64] : packoffset(c70);
};

Texture2D<uint4> u_gbuf : register(t2, space2);
SamplerState _u_gbuf_sampler : register(s2, space2);
Texture2DArray<float4> u_atlas : register(t3, space2);
SamplerState _u_atlas_sampler : register(s3, space2);
Texture2D<float4> u_up : register(t0, space2);
SamplerState _u_up_sampler : register(s0, space2);
Texture2D<float4> u_lit : register(t1, space2);
SamplerState _u_lit_sampler : register(s1, space2);

static float4 gl_FragCoord;
static float4 frag;

struct SPIRV_Cross_Input
{
    float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
    float4 frag : SV_Target0;
};

uint2 spvTextureSize(Texture2D<float4> Tex, uint Level, out uint Param)
{
    uint2 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, Param);
    return ret;
}

uint3 spvTextureSize(Texture2DArray<float4> Tex, uint Level, out uint Param)
{
    uint3 ret;
    Tex.GetDimensions(Level, ret.x, ret.y, ret.z, Param);
    return ret;
}

uint code_at(int2 q)
{
    bool _30 = q.x < 0;
    bool _38;
    if (!_30)
    {
        _38 = q.y < 0;
    }
    else
    {
        _38 = _30;
    }
    bool _57;
    if (!_38)
    {
        _57 = q.x >= _51_u_rect.z;
    }
    else
    {
        _57 = _38;
    }
    bool _67;
    if (!_57)
    {
        _67 = q.y >= _51_u_rect.w;
    }
    else
    {
        _67 = _57;
    }
    if (_67)
    {
        return 4294967295u;
    }
    return u_gbuf.Load(int3(q, 0)).x & 16777215u;
}

float3 atlas_lowres(float2 uv, float layer)
{
    uint _95_dummy_parameter;
    float2 px = 1.0f.xx / float2(int3(spvTextureSize(u_atlas, uint(0), _95_dummy_parameter)).xy);
    float3 c = 0.0f.xxx;
    for (int j = -1; j <= 2; j++)
    {
        for (int i = -1; i <= 2; i++)
        {
            c += u_atlas.Sample(_u_atlas_sampler, float3(uv + (float2(float(i) - 0.5f, float(j) - 0.5f) * px), layer)).xyz;
        }
    }
    return c / 16.0f.xxx;
}

void frag_main()
{
    int2 p3 = int2(gl_FragCoord.xy);
    float4 up = u_up.Load(int3(p3, 0));
    float scale = _51_u_hd.z;
    float2 p1f = (float2(p3) + 0.5f.xx) / scale.xx;
    int2 p1 = int2(p1f);
    int2 q = p1 - _51_u_rect.xy;
    int2 param = q;
    uint code = code_at(param);
    bool _199 = code == 4294967295u;
    bool _209;
    if (!_199)
    {
        _209 = ((code >> uint(16)) & 15u) != 0u;
    }
    else
    {
        _209 = _199;
    }
    bool _217;
    if (!_209)
    {
        _217 = (code & 65535u) == 0u;
    }
    else
    {
        _217 = _209;
    }
    if (_217)
    {
        frag = up;
        return;
    }
    for (int j = -1; j <= 1; j++)
    {
        for (int i = -1; i <= 1; i++)
        {
            int2 param_1 = q + int2(i, j);
            uint c = code_at(param_1);
            bool _249 = c == 4294967295u;
            bool _257;
            if (!_249)
            {
                _257 = ((c >> uint(16)) & 15u) != 0u;
            }
            else
            {
                _257 = _249;
            }
            bool _264;
            if (!_257)
            {
                _264 = (c & 65535u) == 0u;
            }
            else
            {
                _264 = _257;
            }
            if (_264)
            {
                frag = up;
                return;
            }
        }
    }
    float layer = float((code & 65535u) - 1u);
    float2 c0 = _51_u_hd.xy + float2(39.0f, 20.0f);
    float2 d = (float2(q) + frac(p1f)) - c0;
    float a = round(((d.x / 40.0f) + (d.y / 20.0f)) * 0.5f);
    float b = round(((d.y / 20.0f) - (d.x / 40.0f)) * 0.5f);
    float2 center = (c0 + (float2(40.0f, 20.0f) * a)) + (float2(-40.0f, 20.0f) * b);
    float2 local = ((float2(q) + frac(p1f)) - center) + float2(39.0f, 20.0f);
    float2 uv = local / float2(78.0f, 40.0f);
    bool _345 = any(bool2(uv.x < 0.0f.xx.x, uv.y < 0.0f.xx.y));
    bool _353;
    if (!_345)
    {
        _353 = any(bool2(uv.x > 1.0f.xx.x, uv.y > 1.0f.xx.y));
    }
    else
    {
        _353 = _345;
    }
    if (_353)
    {
        frag = up;
        return;
    }
    float4 hd = u_atlas.Sample(_u_atlas_sampler, float3(uv, layer));
    if (hd.w < 0.5f)
    {
        frag = up;
        return;
    }
    float2 param_2 = uv;
    float param_3 = layer;
    float3 base = max(atlas_lowres(param_2, param_3), 0.02999999932944774627685546875f.xxx);
    uint _388_dummy_parameter;
    float3 lit = u_lit.Sample(_u_lit_sampler, p1f / float2(int2(spvTextureSize(u_lit, uint(0), _388_dummy_parameter)))).xyz;
    float3 ratio = clamp(lit / base, 0.0f.xxx, 3.0f.xxx);
    frag = float4(clamp(hd.xyz * ratio, 0.0f.xxx, 1.0f.xxx), 1.0f);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    gl_FragCoord = stage_input.gl_FragCoord;
    gl_FragCoord.w = 1.0 / gl_FragCoord.w;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.frag = frag;
    return stage_output;
}
