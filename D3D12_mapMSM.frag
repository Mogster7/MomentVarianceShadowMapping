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
#ifdef PREDEFINED_MACRO
#include "stdmacro_defs.inc"
#endif

struct PsIn
{
    float4 Position : SV_Position;

    float Depth : TARGET;
};

struct PsOut
{
    float4 Moments : SV_TARGET0;
};

PsOut main(PsIn input)
{
	PsOut output;
	float depth = input.Depth;

    float depthSq = depth * depth;

	// Store moments as depth, depth^2, depth^3, depth^4
    output.Moments = float4(depth, depthSq, 
		depthSq * depth, depthSq * depthSq);

	// Perform this magic number matrix multiplication in order to optimize
	// the storage of these values. This improves numerical stability by 
	// maximizing the entropy of the convex hull spanned by the vectors created
	// by the above equation.
    output.Moments = mul( output.Moments, float4x4(
                     -2.07224649f,   13.7948857237f,  0.105877704f,   9.7924062118f,
                     32.23703778f,  -59.4683975703f, -1.9077466311f,-33.7652110555f,
                    -68.571074599f,  82.0359750338f,  9.3496555107f, 47.9456096605f,
                     39.3703274134f,-35.364903257f,  -6.6543490743f,-23.9728048165f));

    output.Moments[0] += 0.035955884801f;

    return output;
}