cbuffer LitUniforms : register(b0, space3)
{
    int4 _87_u_rect : packoffset(c0);
    float4 _87_u_sundir : packoffset(c1);
    float4 _87_u_grade : packoffset(c2);
    float4 _87_u_params : packoffset(c3);
    int4 _87_u_iparams : packoffset(c4);
    float4 _87_u_hd : packoffset(c5);
    float4 _87_u_lpos[64] : packoffset(c6);
    float4 _87_u_lcol[64] : packoffset(c70);
};

Texture2D<uint4> u_gbuf : register(t1, space2);
SamplerState _u_gbuf_sampler : register(s1, space2);
Texture2D<float4> u_geo : register(t2, space2);
SamplerState _u_geo_sampler : register(s2, space2);
Texture2D<float4> u_color : register(t0, space2);
SamplerState _u_color_sampler : register(s0, space2);

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

static uint g_rcls;

uint code_at(int2 q)
{
    bool _66 = q.x < 0;
    bool _74;
    if (!_66)
    {
        _74 = q.y < 0;
    }
    else
    {
        _74 = _66;
    }
    bool _93;
    if (!_74)
    {
        _93 = q.x >= _87_u_rect.z;
    }
    else
    {
        _93 = _74;
    }
    bool _103;
    if (!_93)
    {
        _103 = q.y >= _87_u_rect.w;
    }
    else
    {
        _103 = _93;
    }
    if (_103)
    {
        return 4294967295u;
    }
    return u_gbuf.Load(int3(q, 0)).x & 16777215u;
}

uint cls_of(uint c)
{
    return (c >> uint(16)) & 15u;
}

float3 world_at(int2 q)
{
    float4 g = u_geo.Load(int3(q, 0));
    return float3(float(q.x), g.x * 2.0f, g.y);
}

float ign(float2 p)
{
    return frac(52.98291778564453125f * frac(dot(p, float2(0.067110560834407806396484375f, 0.005837149918079376220703125f))));
}

float luma(int2 p)
{
    return dot(u_color.Load(int3(p, 0)).xyz, float3(0.2989999949932098388671875f, 0.58700001239776611328125f, 0.114000000059604644775390625f));
}

int2 project(float3 w)
{
    return int2(floor(float2(w.x, (w.y * 0.5f) - w.z) + 0.5f.xx));
}

float trace(float3 from, float3 to, uint self_code, int steps, float j)
{
    float occ = 0.0f;
    bool raised = from.z > 0.5f;
    for (int i = 1; i < steps; i++)
    {
        float t = ((float(i) + j) - 0.5f) / float(steps);
        float3 s = lerp(from, to, t.xxx);
        float3 param = s;
        int2 sp = project(param);
        int2 param_1 = sp;
        uint c = code_at(param_1);
        if ((c == 4294967295u) || (c == self_code))
        {
            continue;
        }
        uint param_2 = c;
        uint cls = cls_of(param_2);
        if (((cls == 0u) || (cls == 4u)) || (cls == 15u))
        {
            continue;
        }
        int2 param_3 = sp;
        float3 o = world_at(param_3);
        uint param_4 = c;
        cls = cls_of(param_4);
        bool _256 = (g_rcls == 3u) && (cls == 3u);
        bool _267;
        if (_256)
        {
            _267 = length(o.xy - from.xy) < 90.0f;
        }
        else
        {
            _267 = _256;
        }
        if (_267)
        {
            continue;
        }
        bool _286;
        if (raised)
        {
            _286 = abs(o.y - from.y) < ((cls == 1u) ? 46.0f : 16.0f);
        }
        else
        {
            _286 = raised;
        }
        if (_286)
        {
            continue;
        }
        float dy = abs(o.y - s.y);
        float th = (cls == 3u) ? 22.3999996185302734375f : 14.0f;
        occ = max(occ, 1.0f - smoothstep(th * 0.4000000059604644775390625f, th, dy));
        if (occ >= 1.0f)
        {
            break;
        }
    }
    return 1.0f - occ;
}

float solid_at(float3 s, uint self_code)
{
    float3 param = s;
    int2 sp = project(param);
    int2 param_1 = sp;
    uint c = code_at(param_1);
    if ((c == 4294967295u) || (c == self_code))
    {
        return 0.0f;
    }
    uint param_2 = c;
    uint cls = cls_of(param_2);
    if (((cls == 0u) || (cls == 4u)) || (cls == 15u))
    {
        return 0.0f;
    }
    if ((g_rcls == 3u) && (cls == 3u))
    {
        return 0.0f;
    }
    int2 param_3 = sp;
    float3 o = world_at(param_3);
    return 1.0f - smoothstep(8.3999996185302734375f, 19.6000003814697265625f, abs(o.y - s.y));
}

float ambient_occlusion(float3 w, uint self_code, float j)
{
    float occ = 0.0f;
    for (int k = 0; k < 8; k++)
    {
        float a = (float(k) + j) * 0.785398006439208984375f;
        float2 d = float2(cos(a), sin(a));
        float3 param = w + float3(d * 14.0f, 6.0f);
        uint param_1 = self_code;
        occ += solid_at(param, param_1);
        float3 param_2 = w + float3(d * 30.0f, 14.0f);
        uint param_3 = self_code;
        occ += (0.60000002384185791015625f * solid_at(param_2, param_3));
    }
    return clamp(occ / 12.80000019073486328125f, 0.0f, 1.0f);
}

void frag_main()
{
    int2 p = int2(gl_FragCoord.xy);
    float3 src = u_color.Load(int3(p, 0)).xyz;
    int2 q = p - _87_u_rect.xy;
    int2 param = q;
    uint code = code_at(param);
    bool _478 = code == 4294967295u;
    bool _487;
    if (!_478)
    {
        _487 = (_87_u_iparams.w & 8) != 0;
    }
    else
    {
        _487 = _478;
    }
    if (_487)
    {
        frag = float4(src, 1.0f);
        return;
    }
    uint param_1 = code;
    if (cls_of(param_1) == 15u)
    {
        frag = float4(src, 1.0f);
        return;
    }
    uint param_2 = code;
    uint cls = cls_of(param_2);
    g_rcls = cls;
    bool indoor = ((code >> uint(20)) & 1u) != 0u;
    int2 param_3 = q;
    float3 w = world_at(param_3);
    float2 param_4 = gl_FragCoord.xy;
    float jn = ign(param_4);
    int2 param_5 = p + int2(2, 0);
    int2 param_6 = p - int2(2, 0);
    int2 param_7 = p + int2(2, 1);
    int2 param_8 = p - int2(2, 1);
    int2 param_9 = p + int2(2, -1);
    int2 param_10 = p - int2(2, -1);
    float gx = (0.5f * (luma(param_5) - luma(param_6))) + (0.25f * (((luma(param_7) - luma(param_8)) + luma(param_9)) - luma(param_10)));
    int2 param_11 = p + int2(0, 2);
    int2 param_12 = p - int2(0, 2);
    int2 param_13 = p + int2(1, 2);
    int2 param_14 = p - int2(1, 2);
    int2 param_15 = p + int2(-1, 2);
    int2 param_16 = p - int2(-1, 2);
    float gy = (0.5f * (luma(param_11) - luma(param_12))) + (0.25f * (((luma(param_13) - luma(param_14)) + luma(param_15)) - luma(param_16)));
    gx = clamp(gx * 0.5f, -0.119999997317790985107421875f, 0.119999997317790985107421875f);
    gy = clamp(gy * 0.5f, -0.119999997317790985107421875f, 0.119999997317790985107421875f);
    float3 n;
    if (cls == 0u)
    {
        n = normalize(float3((-gx) * 1.60000002384185791015625f, (-gy) * 1.60000002384185791015625f, 1.0f));
    }
    else
    {
        if (cls == 4u)
        {
            n = normalize(float3(-gx, 0.5f, 0.85000002384185791015625f));
        }
        else
        {
            if (cls == 3u)
            {
                n = normalize(float3((-gx) * 1.5f, 0.85000002384185791015625f, 0.449999988079071044921875f + (gy * 1.5f)));
            }
            else
            {
                int dl = 0;
                int dr = 0;
                for (int i = 1; i <= 20; i++)
                {
                    int2 param_17 = q - int2(i, 0);
                    if (code_at(param_17) != code)
                    {
                        break;
                    }
                    dl = i;
                }
                for (int i_1 = 1; i_1 <= 20; i_1++)
                {
                    int2 param_18 = q + int2(i_1, 0);
                    if (code_at(param_18) != code)
                    {
                        break;
                    }
                    dr = i_1;
                }
                float nx = float(dl - dr) / float((dl + dr) + 2);
                n = normalize(float3((nx * 1.2000000476837158203125f) - gx, sqrt(max(1.0f - (nx * nx), 0.0f)) * 0.800000011920928955078125f, 0.449999988079071044921875f + (gy * 1.5f)));
                uint hi = u_gbuf.Load(int3(q, 0)).x >> uint(24);
                uint na = hi >> uint(4);
                uint nb = hi & 15u;
                if (((((cls == 2u) && (na >= 1u)) && (na <= 14u)) && (nb >= 1u)) && (nb <= 14u))
                {
                    float sx = (float(na) - 7.5f) / 6.5f;
                    float sy = (float(nb) - 7.5f) / 6.5f;
                    float sz = sqrt(max((1.0f - (sx * sx)) - (sy * sy), 0.0500000007450580596923828125f));
                    n = normalize(float3(sx, ((-sy) + (2.0f * sz)) * 0.447200000286102294921875f, ((2.0f * sy) + sz) * 0.447200000286102294921875f));
                }
            }
        }
    }
    if (_87_u_iparams.y == 2)
    {
        float3 _804;
        if (cls == 0u)
        {
            _804 = float3(0.100000001490116119384765625f, 0.1500000059604644775390625f, 0.4000000059604644775390625f);
        }
        else
        {
            float3 _813;
            if (cls == 1u)
            {
                _813 = float3(1.0f, 0.800000011920928955078125f, 0.300000011920928955078125f);
            }
            else
            {
                float3 _821;
                if (cls == 2u)
                {
                    _821 = float3(1.0f, 0.300000011920928955078125f, 0.300000011920928955078125f);
                }
                else
                {
                    bool3 _832 = (cls == 3u).xxx;
                    _821 = float3(_832.x ? float3(0.300000011920928955078125f, 1.0f, 0.4000000059604644775390625f).x : float3(0.699999988079071044921875f, 0.4000000059604644775390625f, 1.0f).x, _832.y ? float3(0.300000011920928955078125f, 1.0f, 0.4000000059604644775390625f).y : float3(0.699999988079071044921875f, 0.4000000059604644775390625f, 1.0f).y, _832.z ? float3(0.300000011920928955078125f, 1.0f, 0.4000000059604644775390625f).z : float3(0.699999988079071044921875f, 0.4000000059604644775390625f, 1.0f).z);
                }
                _813 = _821;
            }
            _804 = _813;
        }
        float3 tint = _804;
        float hh = clamp(w.z / 160.0f, 0.0f, 1.0f);
        frag = float4(lerp(src * 0.25f, tint * (0.25f + (0.75f * hh)), 0.800000011920928955078125f.xxx), 1.0f);
        if (cls == 2u)
        {
            frag = float4((n * 0.5f) + 0.5f.xxx, 1.0f);
        }
        return;
    }
    float3 acc = 0.0f.xxx;
    float3 glow = 0.0f.xxx;
    float shade = 0.0f;
    float _1005;
    for (int i_2 = 0; i_2 < _87_u_iparams.z; i_2++)
    {
        float4 lp = _87_u_lpos[i_2];
        float3 lw = float3(lp.x, lp.y * 2.0f, lp.z);
        float3 d = lw - w;
        bool _904 = cls == 3u;
        bool _911;
        if (_904)
        {
            _911 = d.y < (-6.0f);
        }
        else
        {
            _911 = _904;
        }
        if (_911)
        {
            continue;
        }
        if (cls == 4u)
        {
            continue;
        }
        float dist = length(d);
        if (dist >= lp.w)
        {
            continue;
        }
        float att = 1.0f - (dist / lp.w);
        att *= att;
        float fl = frac(_87_u_lcol[i_2].w);
        float fi = float(i_2);
        att *= (1.0f + (fl * ((0.60000002384185791015625f * sin((_87_u_params.z * 6.099999904632568359375f) + (fi * 1.7000000476837158203125f))) + (0.4000000059604644775390625f * sin((_87_u_params.z * 13.69999980926513671875f) + (fi * 3.900000095367431640625f))))));
        float ndl = (max(dot(n, d / max(dist, 1.0f).xxx), 0.0f) * 0.85000002384185791015625f) + 0.1500000059604644775390625f;
        float3 side = normalize(float3(-d.y, d.x, 0.0f) + 9.9999997473787516355514526367188e-05f.xxx) * 7.0f;
        if ((_87_u_iparams.w & 1) != 0)
        {
            _1005 = 1.0f;
        }
        else
        {
            float3 param_19 = w;
            float3 param_20 = lerp(w, lw + ((side * (jn - 0.5f)) * 2.0f), 0.89999997615814208984375f.xxx);
            uint param_21 = code;
            int param_22 = 16;
            float param_23 = jn;
            float3 param_24 = w;
            float3 param_25 = lerp(w, lw - ((side * (jn - 0.5f)) * 2.0f), 0.89999997615814208984375f.xxx);
            uint param_26 = code;
            int param_27 = 16;
            float param_28 = frac(jn + 0.5f);
            _1005 = 0.5f * (trace(param_19, param_20, param_21, param_22, param_23) + trace(param_24, param_25, param_26, param_27, param_28));
        }
        float sh = _1005;
        acc += (((_87_u_lcol[i_2].xyz * att) * ndl) * sh);
        float strength = dot(_87_u_lcol[i_2].xyz, 0.333000004291534423828125f.xxx);
        shade = max(shade, ((1.0f - sh) * att) * strength);
        bool _1085 = (cls == 0u) && (!indoor);
        bool _1091;
        if (_1085)
        {
            _1091 = _87_u_params.w > 0.0f;
        }
        else
        {
            _1091 = _1085;
        }
        if (_1091)
        {
            float2 ls = float2(lp.x, lp.y);
            float2 dd = float2(q) - ls;
            float below = smoothstep(-4.0f, 10.0f, dd.y);
            float streak = exp((((-dd.x) * dd.x) / 60.0f) - ((dd.y * dd.y) / 3200.0f)) * below;
            glow += ((((_87_u_lcol[i_2].xyz * streak) * sh) * 0.1599999964237213134765625f) * _87_u_params.w);
        }
    }
    for (int i_3 = 0; i_3 < _87_u_iparams.z; i_3++)
    {
        if (_87_u_lcol[i_3].w < 1.0f)
        {
            continue;
        }
        float4 lp_1 = _87_u_lpos[i_3];
        float2 flame = float2(lp_1.x, lp_1.y - lp_1.z);
        float2 dd_1 = float2(q) - flame;
        float r2 = dot(dd_1, dd_1);
        glow += ((_87_u_lcol[i_3].xyz * ((exp((-r2) / 260.0f) * 0.3499999940395355224609375f) + (exp((-r2) / 2400.0f) * 0.119999997317790985107421875f))) * (0.3499999940395355224609375f + (0.64999997615814208984375f * _87_u_params.w)));
    }
    float3 m = acc * (0.699999988079071044921875f / max(_87_u_params.x, 0.3499999940395355224609375f));
    m /= (1.0f.xxx + (m * 0.699999988079071044921875f));
    float3 outc = (src * (1.0f.xxx + (m * 1.5f))) * (1.0f - (0.449999988079071044921875f * min(shade * 1.60000002384185791015625f, 1.0f)));
    bool _1250 = ((_87_u_params.y > 0.0f) && (cls != 4u)) && (!indoor);
    bool _1257;
    if (_1250)
    {
        _1257 = (_87_u_iparams.w & 2) == 0;
    }
    else
    {
        _1257 = _1250;
    }
    if (_1257)
    {
        float3 sd = normalize(_87_u_sundir.xyz);
        float3 t1 = normalize(cross(sd, float3(0.0f, 0.0f, 1.0f)));
        float3 t2 = cross(sd, t1);
        float sh_1 = 0.0f;
        for (int k = 0; k < 3; k++)
        {
            float ang = (float(k) + jn) * 2.094395160675048828125f;
            float3 dir = normalize(sd + (((t1 * cos(ang)) + (t2 * sin(ang))) * 0.070000000298023223876953125f));
            float3 param_29 = w;
            float3 param_30 = w + (dir * _87_u_sundir.w);
            uint param_31 = code;
            int param_32 = 18;
            float param_33 = frac(jn + (float(k) * 0.333000004291534423828125f));
            sh_1 += trace(param_29, param_30, param_31, param_32, param_33);
        }
        sh_1 *= 0.33329999446868896484375f;
        float _1336;
        if (cls == 0u)
        {
            _1336 = 1.0f;
        }
        else
        {
            _1336 = 0.800000011920928955078125f + (0.4000000059604644775390625f * max(dot(n, sd), 0.0f));
        }
        float facing = _1336;
        if (cls == 2u)
        {
            float3 fill = normalize(float3(-sd.x, 1.0f, 0.89999997615814208984375f));
            facing = (0.550000011920928955078125f + (0.5f * max(dot(n, sd), 0.0f))) + (0.37999999523162841796875f * max(dot(n, fill), 0.0f));
        }
        outc *= lerp(1.0f, facing * lerp(0.60000002384185791015625f, 1.0f, sh_1), _87_u_params.y);
    }
    bool _1382 = cls == 4u;
    bool _1390;
    if (!_1382)
    {
        _1390 = (_87_u_iparams.w & 4) != 0;
    }
    else
    {
        _1390 = _1382;
    }
    float _1391;
    if (_1390)
    {
        _1391 = 0.0f;
    }
    else
    {
        float3 param_34 = w;
        uint param_35 = code;
        float param_36 = jn;
        _1391 = ambient_occlusion(param_34, param_35, param_36);
    }
    float ao = _1391;
    outc *= (1.0f - (0.37999999523162841796875f * ao));
    if (!indoor)
    {
        outc *= _87_u_grade.xyz;
    }
    outc += glow;
    frag = float4(min(outc, 1.0f.xxx), 1.0f);
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
