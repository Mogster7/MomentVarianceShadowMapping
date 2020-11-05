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

#define MIN_VARIANCE 0.00001

// Calculate the upper bound of the propabalistic upper bound 
// of the current depth being in an occluded state, given the 
// distribution of depth we have at the texel.
float ChebyshevUpperBound(float2 samplePoint, float pixelDepth)
{
    float2 moments = shadowMap.Sample(miplessSampler, samplePoint).rg;

    // If light (sampled) depth exceeds our pixel depth, it is lit
    if (pixelDepth <= moments.x)
        return 1.0;

    // Check how likely pixel is to be lit using chebyshev's upper bound
    
    float variance = moments.y - (moments.x * moments.x);

    // Make sure variance is never 0 to avoid issues
    variance = max(variance, MIN_VARIANCE);

    float difference = pixelDepth - moments.x;
    float pMax = variance / (difference * difference + variance);
    
    // Resulting shadow coefficient for lighting
    return pMax;
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

        // Angular bias to offset for uneven surfaces 
		float cosTheta=clamp(dot(N,L), -1.0, 1.0);
		float bias = .005 * tan(acos(cosTheta)) ;
		bias = clamp(bias, 0.0, .1);

		float x, y; float sum = 0.0;
		int iterCount = 0;
		
		for (x = -1.5; x <= 1.5; x += 1.0)   
		{
			for (y = -1.5; y <= 1.5; y += 1.0)   
			{
                // Sample around in area, offset by one texel
				float shadowPortion = ChebyshevUpperBound(shadowIndex.xy + 
                    float2(x, y) * float2(1.0 / shadowMapSize.x, 1.0 / shadowMapSize.y), pixelDepth);

                sum += shadowPortion;
				++iterCount;
			}
		}

		float shadowCoef = sum / (float(iterCount));

		Out.color = float4(amb + diffspec * saturate(shadowCoef), 1.0);
        return Out;
	}

	Out.color = float4(diffspec + amb, 1.0);
    return Out;
}