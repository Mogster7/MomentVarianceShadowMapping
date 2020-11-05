/*
 * Copyright (c) 2018-2020 The Forge Interactive Inc.
 * 
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/
#define PI 3.14159265359

cbuffer cbCamera : register(b0, UPDATE_FREQ_PER_FRAME)
{
	float4x4 projView;
	float4 camPos;
	float4 viewportSize;
};

cbuffer cbLight : register(b1, UPDATE_FREQ_PER_FRAME)
{
	float4x4 lightProjView;
	float4 lightPos;
	float4 lightDir;

	float4 lightAmbient;
	float4 lightValue;
}

cbuffer cbObject : register(b2, UPDATE_FREQ_PER_DRAW)
{
	float4x4 world;

	float4 diffuse;
	float3 specular;
	float shininess;
};

cbuffer cbShadowRootConstants : register (b3)
{
    uint2 shadowMapSize;
}; 

struct PsIn
{
    float4 position : SV_POSITION;

    float4 WorldPos : POSITION;
    float4 ShadowCoord : SHADOW_COORD;
    float4 Normal : NORMAL;
    float4 EyeVec : EYE_VECTOR;
    float4 LightVec : LIGHT_VECTOR;
};

struct PsOut
{
    float4 color : COLOR;
};

Texture2D shadowMap : register(t4, UPDATE_FREQ_PER_FRAME);
SamplerState miplessSampler : register(s5);

#define MOMENT_BIAS 0.000003

// Solve the system of linear equations necessary to derive
// the cumulative minimal probability of occlusion at this depth 
float ComputeMSMShadowIntensity(float4 moments,
    float pixelDepth,float depthBias, float momentBias)
{
    float4 b=lerp(moments, float4(0.5f,0.5f,0.5f,0.5f), momentBias);
    float3 z;
    z[0] = pixelDepth - depthBias;
    float L32D22 = mad(-b[0], b[1], b[2]);
    float D22 = mad(-b[0], b[0], b[1]);
    float SquaredDepthVariance = mad(-b[1], b[1], b[3]);
    float D33D22 = dot(float2(SquaredDepthVariance,-L32D22),
                     float2(D22,                  L32D22));
    float InvD22 = 1.0f / D22;
    float L32 = L32D22 * InvD22;
    float3 c=float3(1.0f, z[0], z[0] * z[0]);
    c[1] -= b.x;
    c[2] -= b.y+L32*c[1];
    c[1] *= InvD22;
    c[2] *= D22 / D33D22;
    c[1] -= L32 * c[2];
    c[0] -= dot(c.yz, b.xy);
    float p = c[1] / c[2];
    float q = c[0] / c[2];
    float r = sqrt((p*p*0.25f) -q);
    z[1] =- p * 0.5f - r;
    z[2] =- p * 0.5f + r;

    float4 Switch=
    	(z[2]<z[0])?float4(z[1],z[0],1.0f,1.0f):(
    	(z[1]<z[0])?float4(z[0],z[1],0.0f,1.0f):
    	float4(0.0f,0.0f,0.0f,0.0f));

    float Quotient = (Switch[0]*z[2]-b[0]*(Switch[0]+z[2])+b[1])
                  / ((z[2]-Switch[1])*(z[0]-z[1]));

    return 1.0f -saturate(Switch[2] + Switch[3] * Quotient);
}

// Use this to sample the shadow map in order to return a numerically
// optimized version of the moments that we then reconstruct into their
// expected values. This is to maximize numerical stability across the pipeline.
void SampleMSM(out float4 moments, float2 samplePoint)
{
    float4 momentsOptimized = shadowMap.Sample(
        miplessSampler, samplePoint
    );
    momentsOptimized[0] -= 0.035955884801f;

    moments = mul(momentsOptimized,
        float4x4(0.2227744146f, 0.1549679261f, 0.1451988946f, 0.163127443f,
                 0.0771972861f, 0.1394629426f, 0.2120202157f, 0.2591432266f,
                 0.7926986636f, 0.7963415838f, 0.7258694464f, 0.6539092497f,
                 0.0319417555f,-0.1722823173f,-0.2758014811f,-0.3376131734f));
}



PsOut main (PsIn input) : SV_TARGET
{
    PsOut Out;

    // No 4th diffuse component, no lighting calc
    if (step(diffuse.a, 0.01)) 
    {
        Out.color = diffuse;
        return Out;
    }

    float3 N = normalize(input.Normal.xyz);
    float3 L = normalize(input.LightVec.xyz);
    float3 V = normalize(input.EyeVec.xyz);
    float3 H = normalize(L.xyz + V.xyz);

	float3 Kd = (float3)diffuse;   
    float3 Ks = specular;
    float a = shininess;

    float3 Ia = lightAmbient.rgb;
    float3 Ii = lightValue.rgb;

    // Ambient light calculated as normal
    float3 amb = Ia * Kd;

    // Clamped L dot H
    float LH = max(0.0, dot(L, H));

    // Schlick approximation of fresnel
    float3 F = Ks + (float3(1.0, 1.0, 1.0) - Ks)* pow(1 - LH, 5);

    // Masking term G and part of the BRDF denominator 
    // simplified and approximated
    float G = 1 / pow(LH, 2);

    // Clamped N dot H
    float NH = max(0.0f, dot(N, H));

    // Micro-facet normal distribution term D 
    float D = ((a + 2) * pow(NH, a)) / (2.0 * PI); 

    float3 BRDF = Kd / PI + (F * G * D) / 4;    

    // Both specular and diffuse components in BRDF
    // Second half of the BRDF calculation
    float3 diffspec = Ii * max(0.0, dot(N, L)) * BRDF;

    float4 ShadowCoord = input.ShadowCoord;

    // Get the shadow coordinates position in the
	// shadow frustum
	float3 shadowIndex = ShadowCoord.xyz;

	// // If we are inside the shadow frustum
	if (shadowIndex.z > 0.0 && shadowIndex.z < 1.0 &&
		shadowIndex.x >= 0.0 && shadowIndex.x <= 1.0 && 
		shadowIndex.y >= 0.0 && shadowIndex.y <= 1.0)
	{
		float pixelDepth = shadowIndex.z;

        // Angular bias to offset bias relative to light angle off the normal
		float cosTheta=clamp(dot(N,L), -1.0, 1.0);
		float bias = .005 * tan(acos(cosTheta)) ;
		bias = clamp(bias, 0.0, .1);

		float x, y; float sum = 0.0;
		int iterCount = 0;
		
        // Used to calculate the intensity of shadow illumination
        // by representing a distribution of possible depth values
        // in an area of a given texel.
        float4 moments; 
		for (x = -1.5; x <= 1.5; x += 1.0)   
		{
			for (y = -1.5; y <= 1.5; y += 1.0)   
			{
                // Sample around in area, offset by a quantity
                float2 samplePoint = shadowIndex.xy + 
                    float2(x, y) * float2(1.0 / shadowMapSize.x, 1.0 / shadowMapSize.y);

                // Get moments from sample
                SampleMSM(moments, samplePoint);

                // Solve the system of linear equations in order to find the minimal cumulative probability
                // of the occlusion state 
				float shadowPortion = ComputeMSMShadowIntensity(moments, pixelDepth, bias * 0.15, MOMENT_BIAS);

                sum += shadowPortion;

				++iterCount;
			}
		}

        float shadowCoef = sum / float(iterCount);

		Out.color = float4(amb + diffspec * saturate(shadowCoef), 1.0);
        return Out;
	}

	Out.color = float4(diffspec + amb, 1.0);
    return Out;
}