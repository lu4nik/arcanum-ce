cbuffer LitUniforms : register(b0, space3)
{
    int4 _57_u_rect : packoffset(c0);
    float4 _57_u_sundir : packoffset(c1);
    float4 _57_u_grade : packoffset(c2);
    float4 _57_u_params : packoffset(c3);
    int4 _57_u_iparams : packoffset(c4);
    float4 _57_u_hd : packoffset(c5);
    float4 _57_u_lpos[64] : packoffset(c6);
    float4 _57_u_lcol[64] : packoffset(c70);
};

Texture2D<uint4> u_gbuf : register(t0, space2);
SamplerState _u_gbuf_sampler : register(s0, space2);

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

uint code_at(int2 q)
{
    bool _36 = q.x < 0;
    bool _44;
    if (!_36)
    {
        _44 = q.y < 0;
    }
    else
    {
        _44 = _36;
    }
    bool _63;
    if (!_44)
    {
        _63 = q.x >= _57_u_rect.z;
    }
    else
    {
        _63 = _44;
    }
    bool _73;
    if (!_63)
    {
        _73 = q.y >= _57_u_rect.w;
    }
    else
    {
        _73 = _63;
    }
    if (_73)
    {
        return 4294967295u;
    }
    return u_gbuf.Load(int3(q, 0)).x & 16777215u;
}

uint cls_of(uint c)
{
    return (c >> uint(16)) & 15u;
}

float anchor_of(uint c)
{
    int s = (int(c & 65535u) + _57_u_iparams.x) & 65535;
    if (s >= 32768)
    {
        s -= 65536;
    }
    return float(s);
}

float column_base(int2 q, float a)
{
    int2 param = q;
    uint param_1 = code_at(param);
    if (cls_of(param_1) != 3u)
    {
        return -1000000.0f;
    }
    for (int i = 1; i <= 200; i += 2)
    {
        int2 param_2 = q + int2(0, i);
        uint cc = code_at(param_2);
        if (cc == 4294967295u)
        {
            return -1000000.0f;
        }
        uint param_3 = cc;
        uint k = cls_of(param_3);
        if (k == 0u)
        {
            return clamp(float(q.y + i), a - 26.0f, a + 26.0f);
        }
        if (k != 3u)
        {
            return -1000000.0f;
        }
    }
    return -1000000.0f;
}

void frag_main()
{
    int2 q = int2(gl_FragCoord.xy);
    if ((_57_u_iparams.w & 16) != 0)
    {
        frag = 0.0f.xxxx;
        return;
    }
    int2 param = q;
    uint c = code_at(param);
    uint param_1 = c;
    uint cls = cls_of(param_1);
    float y = float(q.y);
    if (((c == 4294967295u) || (cls == 0u)) || (cls == 15u))
    {
        frag = float4(y, 0.0f, 0.0f, 1.0f);
        return;
    }
    if (cls == 4u)
    {
        frag = float4(y + 110.0f, 110.0f, 4.0f, 1.0f);
        return;
    }
    uint param_2 = c;
    float a = anchor_of(param_2);
    if (cls == 3u)
    {
        int2 param_3 = q;
        float param_4 = a;
        float own = column_base(param_3, param_4);
        if (own > (-100000.0f))
        {
            a = own;
        }
        else
        {
            float bl = -1000000.0f;
            float br = -1000000.0f;
            int dl = 0;
            int dr = 0;
            for (int i = 2; i <= 48; i += 2)
            {
                if (bl < (-100000.0f))
                {
                    int2 param_5 = q - int2(i, 0);
                    float param_6 = a;
                    float b = column_base(param_5, param_6);
                    if (b > (-100000.0f))
                    {
                        bl = b;
                        dl = i;
                    }
                }
                if (br < (-100000.0f))
                {
                    int2 param_7 = q + int2(i, 0);
                    float param_8 = a;
                    float b_1 = column_base(param_7, param_8);
                    if (b_1 > (-100000.0f))
                    {
                        br = b_1;
                        dr = i;
                    }
                }
                if ((bl > (-100000.0f)) && (br > (-100000.0f)))
                {
                    break;
                }
            }
            if ((bl > (-100000.0f)) && (br > (-100000.0f)))
            {
                a = lerp(bl, br, float(dl) / float(dl + dr));
            }
            else
            {
                if (bl > (-100000.0f))
                {
                    a = bl;
                }
                else
                {
                    if (br > (-100000.0f))
                    {
                        a = br;
                    }
                }
            }
        }
    }
    frag = float4(a, max(a - y, 0.0f), float(cls), 1.0f);
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
