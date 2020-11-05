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
struct VsIn
{
	float4 position : POSITION;
	float4 normal : NORMAL;
};

cbuffer cbCamera : register(b0, UPDATE_FREQ_PER_FRAME)
{
	float4x4 projView;
	float4 camPos;
	float4 viewportSize;
    uint4 debugFlags;
};

cbuffer cbLight : register(b1, UPDATE_FREQ_PER_FRAME)
{
	float4x4 lightProjView;
	float4 lightPos;

	float4 lightAmbient;
	float4 lightValue;
};

cbuffer cbObject : register(b2, UPDATE_FREQ_PER_DRAW)
{
	float4x4 world;

	float4 diffuse;
	float3 specular;
	float shininess;
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




PsIn main (VsIn In)
{
	PsIn Out;
	float4x4 tempMat = mul(projView, world);
	Out.position = mul(tempMat, float4(In.position.xyz, 1.0f));
	
	Out.Normal = mul(world, float4(In.normal.xyz, 0.0f));
	Out.WorldPos = mul(world, float4(In.position.xyz, 1.0f));
	Out.EyeVec = camPos - Out.WorldPos;
	Out.LightVec = lightPos - Out.WorldPos;
	
	const float4x4 shift = { 
		0.5, 0.0, 0.0, 0.5,
		0.0, -0.5, 0.0, 0.5,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	};

	float4x4 shadowMatrix = mul(shift, lightProjView);
	// Shadow matrix * world space position per vert
	float4 objWorld = mul(world, float4(In.position.xyz, 1.0f));
	Out.ShadowCoord = mul(shadowMatrix, objWorld);

	return Out;
}
