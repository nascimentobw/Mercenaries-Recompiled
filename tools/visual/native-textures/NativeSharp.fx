// SPDX-License-Identifier: MIT. Optional, user-selected post-processing.
#include "ReShade.fxh"
uniform float Strength < ui_label="Nitidez"; ui_type="slider"; ui_min=0.0; ui_max=0.4; > = 0.24;
uniform float Contrast < ui_label="Contraste"; ui_type="slider"; ui_min=0.95; ui_max=1.10; > = 1.0;
uniform float Saturation < ui_label="Cor"; ui_type="slider"; ui_min=0.95; ui_max=1.15; > = 1.04;
uniform float Gamma < ui_label="Clarear meios tons"; ui_type="slider"; ui_min=0.65; ui_max=1.20; > = 0.84;
uniform float ShadowLift < ui_label="Clarear sombras"; ui_type="slider"; ui_min=0.0; ui_max=0.10; > = 0.035;
float4 NativeSharpPS(float4 p:SV_Position,float2 uv:TEXCOORD):SV_Target {
    float3 c=tex2D(ReShade::BackBuffer,uv).rgb;
    float2 px=ReShade::PixelSize;
    float3 n=(tex2D(ReShade::BackBuffer,uv+float2(px.x,0)).rgb+tex2D(ReShade::BackBuffer,uv-float2(px.x,0)).rgb+
        tex2D(ReShade::BackBuffer,uv+float2(0,px.y)).rgb+tex2D(ReShade::BackBuffer,uv-float2(0,px.y)).rgb)*0.25;
    c+=clamp((c-n)*Strength,-0.025,0.025);
    float l=dot(c,float3(0.2126,0.7152,0.0722));
    c=lerp(l.xxx,c,Saturation);
    c=saturate((c-0.5)*Contrast+0.5);
    l=dot(c,float3(0.2126,0.7152,0.0722));
    // Lift scene shadows while preserving exact black in letterbox bars.
    float gate=1.0-exp(-max(l,0.0)*200.0);
    c=saturate(c+ShadowLift*pow(1.0-saturate(l),2.0)*gate);
    c=pow(c,Gamma.xxx);
    return float4(saturate(c),1);
}
technique NativeSharp { pass { VertexShader=PostProcessVS; PixelShader=NativeSharpPS; } }
