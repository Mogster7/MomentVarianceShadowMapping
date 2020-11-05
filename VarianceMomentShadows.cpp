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
//Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/IResourceLoader.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"

//Math
#include "../../../../Common_3/OS/Math/MathTypes.h"

//ui
#include "../../../../Middleware_3/UI/AppUI.h"

#include "../../../../Common_3/OS/Interfaces/IMemory.h"

// DEFINE STRUCTURES
struct UniformCamData
{
	mat4 mProjectView;
	vec4 mCamPos;
	vec4 mViewportSize;

	// Depth, x, x, x
	uint32_t mDebugFlags[4] = { 0, 0, 0, 0 };
};

struct UniformLightData
{
	mat4 mLightViewProj;
	vec4 mLightPosition = { 0.0f, 20.0f, 30.0f, 0.0f };
	vec4 mLightDir;
	// a is strength
	vec4 mLightAmbient = { 0.2f, 0.2f, 0.2f, 0.0f };
	vec4 mLightValue = { 4.0f, 4.0f, 4.0f, 0.0f };
};

struct LightView
{
	vec2               viewRotation = { 0.0f, 0.0f };
	vec3               viewPosition = { 0.0f, 0.0f, 0.0f };

	mat4 getViewMatrix() const
	{
		mat4 r{ mat4::rotationXY(-viewRotation.getX(), -viewRotation.getY()) };
		vec4 t = r * vec4(-viewPosition, 1.0f);
		r.setTranslation(t.getXYZ());
		return r;
	}

	void moveTo(const vec3& location) { viewPosition = location; }

	void lookAt(const vec3& lookAt)
	{
		vec3 lookDir = normalize(lookAt - viewPosition);

		float y = lookDir.getY();
		viewRotation.setX(-asinf(y));

		float x = lookDir.getX();
		float z = lookDir.getZ();
		float n = sqrtf((x * x) + (z * z));
		if (n > 0.01f)
		{
			// don't change the Y rotation if we're too close to vertical
			x /= n;
			z /= n;
			viewRotation.setY(atan2f(x, z));
		}
	}
};

struct UniformObjectData
{
	mat4 mWorld;

	// Last component determines if lit or not, >0
	vec4 mDiffuse;
	// Last component is shininess
	vec4 mSpecular;
};

struct UniformShadowMapData
{
	uvec2 mSize = { 2048, 2048 };
};


// ----------------------

// VARIABLES
const uint32_t gImageCount = 3;
const uint32_t gMaxBlurs = 8;
const uint32_t gMaxObjectCount = 512;
const int      gSphereResolution = 30;    // Increase for higher resolution spheres
const float    gSphereDiameter = 0.5f;

const vec3 gWoodColor(87.0f / 255.0f, 51.0f / 255.0f, 35.0f / 255.0f);
const vec3 gBrickColor(134.0f / 255.0f, 60.0f / 255.0f, 56.0f / 255.0f);
const vec3 gFloorColor(.75f, .75f, .75f);
const vec3 gBrassColor(0.5f, 0.5f, 0.1f);
const vec3 gGrassColor(62.0f / 255.0f, 102.0f / 255.0f, 38.0f / 255.0f);
const vec3 gWaterColor(0.3f, 0.3f, 1.0f);

const vec3 gBlack(0.0f, 0.0f, 0.0f);
const vec3 gBrightSpec(0.03f, 0.03f, 0.03f);
const vec3 gPolishedSpec(0.02f, 0.02f, 0.02f);
const vec3 gMiniSpec(0.01f, 0.01f, 0.01f);
const vec3	   gPlaneSize = { 75.0f, 1.0f, 75.0f };

bool gToggleVSync = false;
int32_t gToggleMSM = false;

uint32_t gFrameIndex = 0;
uint32_t gBlurCount = 1;

const uint32_t gNumSpheres = 29;
int gNumberOfSpherePoints = 0;
Buffer* pBufferVertexSphere = { NULL };
Buffer* pBufferUniformSphere[gNumSpheres] = { NULL };
UniformObjectData gDataSphere[gNumSpheres] = {};
float gSphereTimers[gNumSpheres] = { 0.0f };
float gSphereBounceModifiers[gNumSpheres] = { 0.0f };
float gBounceSpeed = 1.0f;

const vec3 gPlanePosition = { 0.0f, -3.0f, 0.0f };
int gNumberOfPlanePoints = 0;
Buffer* pBufferVertexPlane = NULL;
Buffer* pBufferUniformPlane = NULL;
UniformObjectData gDataPlane = {};

// Lights
LightView gViewLight;
UniformLightData gDataLight = {};
UniformObjectData gDataLightObject = {};
// Radius, theta, phi
vec3 gLightSphereCoords = { 100.0f, 60.0f, 0.0f };
int gNumberOfLightObjectPoints = 0;
float gLightOrbitSpeed = 1.0f;
float gLightOrbitDistance = 15.0f;
Buffer* pBufferUniformLightObject = NULL;
Buffer* pBufferVertexLightObject = NULL;
Buffer* pBufferUniformLight[gImageCount] = { NULL };

// Shadow
UniformShadowMapData gShadowMapData;

// Camera
UniformCamData gDataCamera = {};
ICameraController* pCameraController = NULL;
Buffer* pBufferUniformCamera[gImageCount] = { NULL };

// Rendering info
Renderer* pRenderer = NULL;
Queue* pGraphicsQueue = NULL;
CmdPool* pCmdPools[gImageCount] = { NULL };
Cmd* pCmds[gImageCount] = { NULL };

SwapChain* pSwapChain = NULL;

RenderTarget* pRenderTargetDepthBuffer = NULL;

RenderTarget* pRenderTargetMapVSM = NULL;
RenderTarget* pRenderTargetMapMSM = NULL;
RenderTarget* pRenderTargetShadowDepth = NULL;

Texture* pTexBlurHorVSM = NULL;
Texture* pTexBlurVertVSM = NULL;
Texture* pTexBlurHorMSM = NULL;
Texture* pTexBlurVertMSM = NULL;

Fence*        pFencesRenderComplete[gImageCount] = { NULL };
Semaphore*    pSemaphoreImageAcquired = NULL;
Semaphore*    pSemaphoresRenderComplete[gImageCount] = { NULL };

Shader* pShaderVSM = NULL;
Shader* pShaderMSM = NULL;
Shader* pShaderMapVSM = NULL;
Shader* pShaderMapMSM = NULL;
Shader* pShaderShadowBlur = NULL;

RootSignature* pRootSignatureVSM = NULL;
RootSignature* pRootSignatureMSM = NULL;
RootSignature* pRootSignatureMapVSM = NULL;
RootSignature* pRootSignatureMapMSM = NULL;
RootSignature* pRootSignatureShadowBlur = NULL;

Pipeline* pPipelineVSM = NULL;
Pipeline* pPipelineMSM = NULL;
Pipeline* pPipelineMapVSM = NULL;
Pipeline* pPipelineMapMSM = NULL;
Pipeline* pPipelineShadowBlur[gMaxBlurs][2] = { NULL };

DescriptorSet* pDescriptorSetVSM[3] = { NULL };
DescriptorSet* pDescriptorSetMSM[3] = { NULL };
DescriptorSet* pDescriptorSetMapVSM[3] = { NULL };
DescriptorSet* pDescriptorSetMapMSM[3] = { NULL };
DescriptorSet* pDescriptorSetShadowBlur[3] = { NULL };

Sampler* pSamplerBilinear = NULL;
Sampler* pSamplerMipless = NULL;

// Profiling
ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

// UI
UIApp gAppUI = {};
GuiComponent* pGui = NULL;
TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff00ffff, 18);
VirtualJoystickUI gVirtualJoystick = {};

// ------------------------------------

class MomentShadows : public IApp
{
public:
	MomentShadows()
	{
		gToggleVSync = mSettings.mDefaultVSyncEnabled;
	}

	bool Init()
	{
		// FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_SOURCES, "Shaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG, "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES, "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");

		// window and renderer setup
		RendererDesc settings = { 0 };
		initRenderer(GetName(), &settings, &pRenderer);
		//check for init success
		if (!pRenderer)
			return false;

		// Set up the graphics queue
		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			CmdPoolDesc cmdPoolDesc = {};
			cmdPoolDesc.pQueue = pGraphicsQueue;
			addCmdPool(pRenderer, &cmdPoolDesc, &pCmdPools[i]);
			CmdDesc cmdDesc = {};
			cmdDesc.pPool = pCmdPools[i];
			addCmd(pRenderer, &cmdDesc, &pCmds[i]);

			addFence(pRenderer, &pFencesRenderComplete[i]);
			addSemaphore(pRenderer, &pSemaphoresRenderComplete[i]);
		}
		addSemaphore(pRenderer, &pSemaphoreImageAcquired);

		initResourceLoaderInterface(pRenderer);

		// Initialize virtual joystick
		if (!gVirtualJoystick.Init(pRenderer, "circlepad"))
		{
			LOGF(LogLevel::eERROR, "Could not initialize Virtual Joystick.");
			return false;
		}

		ShaderLoadDesc shaderVSM = {};
		shaderVSM.mStages[0] = { "basic.vert", NULL, 0 };
		shaderVSM.mStages[1] = { "VSM.frag", NULL, 0 };
		addShader(pRenderer, &shaderVSM, &pShaderVSM);

		ShaderLoadDesc shaderMSM = {};
		shaderMSM.mStages[0] = { "basic.vert", NULL, 0 };
		shaderMSM.mStages[1] = { "MSM.frag", NULL, 0 };
		addShader(pRenderer, &shaderMSM, &pShaderMSM);


		ShaderLoadDesc shaderMapVSM = {};
		shaderMapVSM.mStages[0] = { "shadowPass.vert", NULL, 0 };
		shaderMapVSM.mStages[1] = { "mapVSM.frag", NULL, 0 };
		addShader(pRenderer, &shaderMapVSM, &pShaderMapVSM);

		ShaderLoadDesc shaderMapMSM = {};
		shaderMapMSM.mStages[0] = { "shadowPass.vert", NULL, 0 };
		shaderMapMSM.mStages[1] = { "mapMSM.frag", NULL, 0 };
		addShader(pRenderer, &shaderMapMSM, &pShaderMapMSM);


		ShaderLoadDesc shaderShadowBlur = {};
		shaderShadowBlur.mStages[0] = { "shadowBlur.comp", NULL, 0 };
		addShader(pRenderer, &shaderShadowBlur, &pShaderShadowBlur);


		SamplerDesc clampMiplessSamplerDesc = {};
		clampMiplessSamplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_EDGE;
		clampMiplessSamplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_EDGE;
		clampMiplessSamplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_EDGE;
		clampMiplessSamplerDesc.mMinFilter = FILTER_LINEAR;
		clampMiplessSamplerDesc.mMagFilter = FILTER_LINEAR;
		clampMiplessSamplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
		clampMiplessSamplerDesc.mMipLodBias = 0.0f;
		clampMiplessSamplerDesc.mMaxAnisotropy = 0.0f;
		addSampler(pRenderer, &clampMiplessSamplerDesc, &pSamplerMipless);

		SamplerDesc samplerDesc = { FILTER_LINEAR,       FILTER_LINEAR,       MIPMAP_MODE_LINEAR,
									ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT };
		addSampler(pRenderer, &samplerDesc, &pSamplerBilinear);


		/************************************************************************/
		// Root sigs
		/************************************************************************/
		const char*       pStaticSamplerNames[] = { "miplessSampler" };
		Sampler* pStaticSamplers[] = { pSamplerMipless };

		// Main render passes
		RootSignatureDesc rootDesc = { &pShaderVSM, 1 };
		rootDesc.mStaticSamplerCount = 1;
		rootDesc.ppStaticSamplerNames = pStaticSamplerNames;
		rootDesc.ppStaticSamplers = pStaticSamplers;
		addRootSignature(pRenderer, &rootDesc, &pRootSignatureVSM);

		rootDesc.ppShaders = &pShaderMSM;
		addRootSignature(pRenderer, &rootDesc, &pRootSignatureMSM);


		// Shadow blur
		rootDesc = { &pShaderShadowBlur, 1 };
		addRootSignature(pRenderer, &rootDesc, &pRootSignatureShadowBlur);

		// Shadow mapping
		rootDesc = { &pShaderMapVSM, 1 };
		addRootSignature(pRenderer, &rootDesc, &pRootSignatureMapVSM);

		rootDesc = { &pShaderMapMSM, 1 };
		addRootSignature(pRenderer, &rootDesc, &pRootSignatureMapMSM);


		/************************************************************************/
		// Descriptor Sets
		/************************************************************************/

		// Rendering sets
		DescriptorSetDesc desc = { pRootSignatureVSM, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetVSM[0]);
		desc = { pRootSignatureVSM, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetVSM[1]);
		desc = { pRootSignatureVSM, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gMaxObjectCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetVSM[2]);

		// Rendering sets
		desc = { pRootSignatureMSM, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMSM[0]);
		desc = { pRootSignatureMSM, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMSM[1]);
		desc = { pRootSignatureMSM, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gMaxObjectCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMSM[2]);


		// Shadow pass set
		desc = { pRootSignatureMapVSM, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMapVSM[0]);
		desc = { pRootSignatureMapVSM, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gMaxObjectCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMapVSM[1]);

		desc = { pRootSignatureMapMSM, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMapMSM[0]);
		desc = { pRootSignatureMapMSM, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gMaxObjectCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetMapMSM[1]);



		// Shadow blur set
		desc = { pRootSignatureShadowBlur, DESCRIPTOR_UPDATE_FREQ_NONE, gMaxBlurs * 2 * gImageCount };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetShadowBlur[0]);
		desc = { pRootSignatureShadowBlur, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gMaxBlurs * 2 * gImageCount};
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetShadowBlur[1]);


		// Generate sphere vertex buffer
		float* pSpherePoints;
		generateSpherePoints(&pSpherePoints, &gNumberOfSpherePoints, gSphereResolution, gSphereDiameter);

		// Generate plane vertex buffer
		float* pPlanePoints;
		generateCuboidPoints(&pPlanePoints, &gNumberOfPlanePoints,
			gPlaneSize.getX(),
			gPlaneSize.getY(),
			gPlaneSize.getZ(),
			vec3(0.0f));

		float* pLightObjPoints;
		generateCuboidPoints(&pLightObjPoints, &gNumberOfLightObjectPoints, 3.0f, 3.0f, 3.0f, vec3(0.0f));

		// vertex data
		// Common among all
		BufferLoadDesc vbDesc = {};
		vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;

		// Sphere
		uint64_t       sphereDataSize = gNumberOfSpherePoints * sizeof(float);
		vbDesc.mDesc.mSize = sphereDataSize;
		vbDesc.pData = pSpherePoints;
		vbDesc.ppBuffer = &pBufferVertexSphere;
		addResource(&vbDesc, NULL);

		// Plane 
		uint64_t       planeDataSize = gNumberOfPlanePoints * sizeof(float);
		vbDesc.mDesc.mSize = planeDataSize;
		vbDesc.pData = pPlanePoints;
		vbDesc.ppBuffer = &pBufferVertexPlane;
		addResource(&vbDesc, NULL);

		// Light object
		uint64_t       lightObjectDataSize = gNumberOfLightObjectPoints * sizeof(float);
		vbDesc.mDesc.mSize = lightObjectDataSize;
		vbDesc.pData = pLightObjPoints;
		vbDesc.ppBuffer = &pBufferVertexLightObject;;
		addResource(&vbDesc, NULL);


		// object uniform data
		BufferLoadDesc ubObjectDesc = {};
		ubObjectDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubObjectDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubObjectDesc.mDesc.mSize = sizeof(UniformObjectData);
		ubObjectDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubObjectDesc.pData = NULL;
		for (int i = 0; i < gNumSpheres; ++i)
		{
			ubObjectDesc.ppBuffer = &pBufferUniformSphere[i];
			addResource(&ubObjectDesc, NULL);
		}

		ubObjectDesc.ppBuffer = &pBufferUniformPlane;
		addResource(&ubObjectDesc, NULL);

		ubObjectDesc.ppBuffer = &pBufferUniformLightObject;
		addResource(&ubObjectDesc, NULL);

		// Uniform buffer for camera data
		BufferLoadDesc ubCamDesc = {};
		ubCamDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubCamDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubCamDesc.mDesc.mSize = sizeof(UniformCamData);
		ubCamDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubCamDesc.pData = NULL;

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			ubCamDesc.ppBuffer = &pBufferUniformCamera[i];
			addResource(&ubCamDesc, NULL);
		}

		// Uniform buffer for light data
		BufferLoadDesc ubLightDesc = {};
		ubLightDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubLightDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubLightDesc.mDesc.mSize = sizeof(UniformLightData);
		ubLightDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubLightDesc.pData = NULL;

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			ubLightDesc.ppBuffer = &pBufferUniformLight[i];
			addResource(&ubLightDesc, NULL);
		}


		// Init input system
		if (!initInputSystem(pWindow))
			return false;

		// Initialize microprofiler and it's UI.
		initProfiler();

		// Gpu profiler can only be added after initProfile.
		gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");


		// Create UI
		if (!gAppUI.Init(pRenderer))
			return false;

		gAppUI.LoadFont("TitilliumText/TitilliumText-Bold.otf");

		/************************************************************************/
		// Camera
		/************************************************************************/
		CameraMotionParameters camParameters{ 100.0f, 150.0f, 300.0f };
		vec3                   camPos{ 0.0f, 5.0f, -10.0f };
		vec3                   lookAt{ 0.0f, 0.0f, 0.0f };

		pCameraController = createFpsCameraController(camPos, lookAt);

		pCameraController->setMotionParameters(camParameters);

		/************************************************************************/
		// GUI
		/************************************************************************/
		GuiDesc guiDesc = {};
		guiDesc.mStartPosition = vec2(mSettings.mWidth * 0.01f, mSettings.mHeight * 0.25f);
		pGui = gAppUI.AddGuiComponent(GetName(), &guiDesc);
#if !defined(TARGET_IOS)
		pGui->AddWidget(CheckboxWidget("Toggle VSync\t\t\t\t\t", &gToggleVSync));
#endif

		SliderFloat3Widget lightVal("Light Intensity", (float3*)&gDataLight.mLightValue,
			float3(0.0f), float3(100.0f), float3(0.5f));
		SliderFloat3Widget lightAmb("Light Ambient", (float3*)&gDataLight.mLightAmbient,
			float3(0.0f), float3(1.0f));
		//SliderFloat3Widget lightPos("Light Position", (float3*)&gDataLight.mLightPosition,
		//	float3(-200.0f, -200.0f, -200.0f), float3(200.0f, 200.0f, 200.0f));
		//SliderFloatWidget lightOrbitDist("Light Orbit Distance", (float*)&gLightOrbitDistance,
		//	1.0f, 50.0f);
		SliderFloatWidget lightRad("Light Radius", (float*)(&gLightSphereCoords), 50.1f, 200.0f);
		SliderFloatWidget lightAng("Light Angle", ((float*)&gLightSphereCoords) + 1, 20.0f, 160.0f);
		SliderFloatWidget lightAz("Light Azimuth", ((float*)&gLightSphereCoords) + 2, -179.9f, 179.9f);


		//SliderFloatWidget lightOrbitSpeed("Light Orbit Speed", (float*)&gLightOrbitSpeed,
		//	0.0f, 10.0f);

		SliderFloatWidget bounceSpeed("Bounce Speed", &gBounceSpeed, 0.0f, 20.0f);
		//CheckboxWidget debugDepth("Debug Depth", (bool*)&gDataCamera.mDebugFlags[0]);
		//CheckboxWidget debugSF("Debug Shadow Frustum", (bool*)&gDataCamera.mDebugFlags[1]);
		SliderUintWidget blurPasses("Gaussian Filter Shadow Passes", &gBlurCount, 0, gMaxBlurs);


		pGui->AddWidget(lightAmb);
		pGui->AddWidget(lightVal);
		//pGui->AddWidget(lightOrbitDist);
		//pGui->AddWidget(lightOrbitSpeed);
		pGui->AddWidget(lightRad);
		pGui->AddWidget(lightAng);
		pGui->AddWidget(lightAz);
		pGui->AddWidget(bounceSpeed);
		pGui->AddWidget(blurPasses);
		//pGui->AddWidget(debugDepth);
		//pGui->AddWidget(debugSF);

		const char* labels[] = {
			"Variance Shadow Mapping",
			"Moment Shadow Mapping"
		};

		for (int i = 0; i < 2; ++i)
		{
			pGui->AddWidget(RadioButtonWidget(labels[i], (int32_t*)&gToggleMSM, i));
		}



		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx) { toggleFullscreen(((IApp*)ctx->pUserData)->pWindow); return true; }, this };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
		addInputAction(&actionDesc);
		actionDesc =
		{
			InputBindings::BUTTON_ANY, [](InputActionContext* ctx)
			{
				bool capture = gAppUI.OnButton(ctx->mBinding, ctx->mBool, ctx->pPosition);
				setEnableCaptureInput(capture && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);
				return true;
			}, this
		};
		addInputAction(&actionDesc);
		typedef bool(*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
		static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
		{
			if (!gAppUI.IsFocused() && *ctx->pCaptured)
			{
				gVirtualJoystick.OnMove(index, ctx->mPhase != INPUT_ACTION_PHASE_CANCELED, ctx->pPosition);
				index ? pCameraController->onRotate(ctx->mFloat2) : pCameraController->onMove(ctx->mFloat2);
			}
			return true;
		};
		actionDesc = { InputBindings::FLOAT_RIGHTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::FLOAT_LEFTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_NORTH, [](InputActionContext* ctx) { pCameraController->resetView(); return true; } };
		addInputAction(&actionDesc);

		tf_free(pSpherePoints);
		tf_free(pLightObjPoints);
		tf_free(pPlanePoints);

		waitForAllResourceLoads();
		PrepareResources();


		return true;
	}

	void Exit()
	{
		waitQueueIdle(pGraphicsQueue);

		exitInputSystem();

		destroyCameraController(pCameraController);

		gVirtualJoystick.Exit();

		gAppUI.Exit();

		// Exit profile
		exitProfiler();

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeResource(pBufferUniformLight[i]);
			removeResource(pBufferUniformCamera[i]);
		}

		for (int i = 0; i < 3; ++i)
		{
			removeDescriptorSet(pRenderer, pDescriptorSetVSM[i]);
			removeDescriptorSet(pRenderer, pDescriptorSetMSM[i]);

			if (i < 2)
			{
				removeDescriptorSet(pRenderer, pDescriptorSetMapVSM[i]);
				removeDescriptorSet(pRenderer, pDescriptorSetMapMSM[i]);
				removeDescriptorSet(pRenderer, pDescriptorSetShadowBlur[i]);
			}
		}

		removeResource(pBufferVertexPlane);
		removeResource(pBufferVertexSphere);
		removeResource(pBufferVertexLightObject);
		removeResource(pBufferUniformPlane);
		removeResource(pBufferUniformLightObject);
		removeResource(pTexBlurHorVSM);
		removeResource(pTexBlurVertVSM);
		removeResource(pTexBlurHorMSM);
		removeResource(pTexBlurVertMSM);

		for (int i = 0; i < gNumSpheres; ++i)
		{
			removeResource(pBufferUniformSphere[i]);
		}


		removeSampler(pRenderer, pSamplerBilinear);
		removeSampler(pRenderer, pSamplerMipless);
		removeShader(pRenderer, pShaderVSM);
		removeShader(pRenderer, pShaderMSM);
		removeShader(pRenderer, pShaderMapVSM);
		removeShader(pRenderer, pShaderMapMSM);
		removeShader(pRenderer, pShaderShadowBlur);
		removeRootSignature(pRenderer, pRootSignatureVSM);
		removeRootSignature(pRenderer, pRootSignatureMSM);
		removeRootSignature(pRenderer, pRootSignatureMapVSM);
		removeRootSignature(pRenderer, pRootSignatureMapMSM);
		removeRootSignature(pRenderer, pRootSignatureShadowBlur);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pFencesRenderComplete[i]);
			removeSemaphore(pRenderer, pSemaphoresRenderComplete[i]);

			removeCmd(pRenderer, pCmds[i]);
			removeCmdPool(pRenderer, pCmdPools[i]);
		}
		removeSemaphore(pRenderer, pSemaphoreImageAcquired);

		exitResourceLoaderInterface(pRenderer);
		removeQueue(pRenderer, pGraphicsQueue);
		removeRenderer(pRenderer);

	}

	bool Load()
	{
		if (!addSwapChain())
			return false;

		if (!addRenderTargets())
			return false;

		if (!gAppUI.Load(pSwapChain->ppRenderTargets, 1))
			return false;

		if (!gVirtualJoystick.Load(pSwapChain->ppRenderTargets[0]))
			return false;

		loadProfilerUI(&gAppUI, mSettings.mWidth, mSettings.mHeight);

		//layout and pipeline for sphere draw
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

		// Layout for shadow map
		VertexLayout vertexLayoutPositionOnly = {};
		vertexLayoutPositionOnly.mAttribCount = 1;
		vertexLayoutPositionOnly.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayoutPositionOnly.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayoutPositionOnly.mAttribs[0].mBinding = 0;
		vertexLayoutPositionOnly.mAttribs[0].mLocation = 0;
		vertexLayoutPositionOnly.mAttribs[0].mOffset = 0;


		RasterizerStateDesc shadowRasterizerStateDesc = {};
		shadowRasterizerStateDesc.mCullMode = CULL_MODE_FRONT;

		RasterizerStateDesc basicRasterizerStateDesc = {};
		basicRasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = CMP_LEQUAL;

		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;

		// SHADOW PASS
		desc.mGraphicsDesc = {};
		GraphicsPipelineDesc& shadowPassPipelineSettings = desc.mGraphicsDesc;
		shadowPassPipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		shadowPassPipelineSettings.mRenderTargetCount = 1;
		shadowPassPipelineSettings.pColorFormats = &pRenderTargetMapVSM->mFormat;
		shadowPassPipelineSettings.pDepthState = &depthStateDesc;
		shadowPassPipelineSettings.mSampleCount = pRenderTargetShadowDepth->mSampleCount;
		shadowPassPipelineSettings.mSampleQuality = pRenderTargetShadowDepth->mSampleQuality;
		shadowPassPipelineSettings.mDepthStencilFormat = pRenderTargetShadowDepth->mFormat;
		shadowPassPipelineSettings.pRootSignature = pRootSignatureMapVSM;
		shadowPassPipelineSettings.pRasterizerState = &shadowRasterizerStateDesc;
		shadowPassPipelineSettings.pVertexLayout = &vertexLayoutPositionOnly;
		shadowPassPipelineSettings.pShaderProgram = pShaderMapVSM;
		addPipeline(pRenderer, &desc, &pPipelineMapVSM);

		shadowPassPipelineSettings.pColorFormats = &pRenderTargetMapMSM->mFormat;
		shadowPassPipelineSettings.pShaderProgram = pShaderMapMSM;
		addPipeline(pRenderer, &desc, &pPipelineMapMSM);


		// BLUR
		PipelineDesc computeDesc = {};
		computeDesc.mType = PIPELINE_TYPE_COMPUTE;

		ComputePipelineDesc& shadowBlurPipelineSettings = computeDesc.mComputeDesc;
		shadowBlurPipelineSettings.pRootSignature = pRootSignatureShadowBlur;
		shadowBlurPipelineSettings.pShaderProgram = pShaderShadowBlur;

		for (int i = 0; i < gMaxBlurs; ++i)
		{
			addPipeline(pRenderer, &computeDesc, &pPipelineShadowBlur[i][0]);
			addPipeline(pRenderer, &computeDesc, &pPipelineShadowBlur[i][1]);
		}



		// MAIN RENDER
		desc.mGraphicsDesc = {};
		GraphicsPipelineDesc& pipelineVSM = desc.mGraphicsDesc;
		pipelineVSM.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineVSM.mRenderTargetCount = 1;
		pipelineVSM.pDepthState = &depthStateDesc;
		pipelineVSM.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineVSM.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineVSM.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineVSM.mDepthStencilFormat = pRenderTargetDepthBuffer->mFormat;
		pipelineVSM.pRootSignature = pRootSignatureVSM;
		pipelineVSM.pShaderProgram = pShaderVSM;
		pipelineVSM.pVertexLayout = &vertexLayout;
		pipelineVSM.pRasterizerState = &basicRasterizerStateDesc;
		addPipeline(pRenderer, &desc, &pPipelineVSM);

		GraphicsPipelineDesc& pipelineMSM = desc.mGraphicsDesc;
		pipelineMSM.pShaderProgram = pShaderMSM;
		pipelineMSM.pRootSignature = pRootSignatureMSM;
		addPipeline(pRenderer, &desc, &pPipelineMSM);

		

		PrepareDescriptorSets();

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pGraphicsQueue);

		unloadProfilerUI();
		gAppUI.Unload();

		gVirtualJoystick.Unload();

		removePipeline(pRenderer, pPipelineVSM);
		removePipeline(pRenderer, pPipelineMSM);
		removePipeline(pRenderer, pPipelineMapVSM);
		removePipeline(pRenderer, pPipelineMapMSM);
		for (int i = 0; i < gMaxBlurs; ++i)
		{
			removePipeline(pRenderer, pPipelineShadowBlur[i][0]);
			removePipeline(pRenderer, pPipelineShadowBlur[i][1]);
		}

		removeSwapChain(pRenderer, pSwapChain);

		removeRenderTarget(pRenderer, pRenderTargetDepthBuffer);
		removeRenderTarget(pRenderer, pRenderTargetShadowDepth);
		removeRenderTarget(pRenderer, pRenderTargetMapVSM);
		removeRenderTarget(pRenderer, pRenderTargetMapMSM);
	}

	void Update(float deltaTime)
	{
#if !defined(TARGET_IOS)
		if (pSwapChain->mEnableVsync != gToggleVSync)
		{
			waitQueueIdle(pGraphicsQueue);
			gFrameIndex = 0;
			::toggleVSync(pRenderer, &pSwapChain);
		}
#endif

		updateInputSystem(mSettings.mWidth, mSettings.mHeight);
		pCameraController->update(deltaTime);

		mat4 viewMat = pCameraController->getViewMatrix();

		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.0f;
		mat4        projMat = mat4::perspective(horizontal_fov, aspectInverse, 1.0f, 1000.0f);
		gDataCamera.mProjectView = projMat * viewMat;
		gDataCamera.mCamPos = vec4(pCameraController->getViewPosition(), 0.0f);

		mat4 identity = mat4::identity();
		// bounce the spheres yes
		for (int i = 0; i < gNumSpheres; ++i)
		{
			vec3 translate = gDataSphere[i].mWorld.getCol3().getXYZ();
			translate.setY(gPlanePosition.getY() + 1.0f + abs(sinf(gSphereTimers[i] * gSphereBounceModifiers[i]) * 4.0f));
			gDataSphere[i].mWorld = identity.translation(translate);

			gSphereTimers[i] += deltaTime * gBounceSpeed * 0.4f;
		}


		// Light updates
		vec3 lightPosVec = SphericalToCartesian(gLightSphereCoords);
		gViewLight.moveTo({ 0.0f, 0.0f, 0.0f });
		gViewLight.lookAt(normalize(vec3(0.0f) - lightPosVec));


		vec3 diff = gDataLight.mLightValue.getXYZ();
		diff = normalize(diff);
		gDataLightObject.mDiffuse = vec4(diff, 0.0f); // 0.0f means not calculated by lighting

		// directional lighting model
		mat4 lightViewProj = mat4::orthographic(-15, 15, -15, 15, -gPlaneSize.getZ() * 0.25f, gPlaneSize.getZ() * 0.75f) * gViewLight.getViewMatrix();

		gDataLight.mLightViewProj = lightViewProj;
		gDataLight.mLightPosition = vec4(lightPosVec, 1.0f);
		gDataLightObject.mWorld = identity.translation(lightPosVec);


		gAppUI.Update(deltaTime);
	}

	void Draw()
	{
		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pSemaphoreImageAcquired, NULL, &swapchainImageIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		Semaphore*    pSemaphoreRenderComplete = pSemaphoresRenderComplete[gFrameIndex];
		Fence*        pFenceRenderComplete = pFencesRenderComplete[gFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pFenceRenderComplete, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pFenceRenderComplete);

		// Reset cmd pool for this frame
		resetCmdPool(pRenderer, pCmdPools[gFrameIndex]);

		/************************************************************************/
		// Update Uniform Buffers
		/************************************************************************/
		BufferUpdateDesc camCbv = { pBufferUniformCamera[gFrameIndex] };
		gDataCamera.mViewportSize = vec4((float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 0.0f);
		beginUpdateResource(&camCbv);
		*(UniformCamData*)camCbv.pMappedData = gDataCamera;
		endUpdateResource(&camCbv, NULL);

		BufferUpdateDesc lightCbv = { pBufferUniformLight[gFrameIndex] };
		beginUpdateResource(&lightCbv);
		*(UniformLightData*)lightCbv.pMappedData = gDataLight;
		endUpdateResource(&lightCbv, NULL);

		for (int i = 0; i < gNumSpheres; ++i)
		{
			BufferUpdateDesc sphereCbv = { pBufferUniformSphere[i] };
			beginUpdateResource(&sphereCbv);
			*(UniformObjectData*)sphereCbv.pMappedData = gDataSphere[i];
			endUpdateResource(&sphereCbv, NULL);
		}

		BufferUpdateDesc lightObjectCbv = { pBufferUniformLightObject };
		beginUpdateResource(&lightObjectCbv);
		*(UniformObjectData*)lightObjectCbv.pMappedData = gDataLightObject;
		endUpdateResource(&lightObjectCbv, NULL);

		/************************************************************************/
		// Begin Render 
		/************************************************************************/
		Cmd* cmd = pCmds[gFrameIndex];
		beginCmd(cmd);

		/************************************************************************/
		// Draw Objects
		/************************************************************************/
		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		/************************************************************************/
		// Shadow Map pass
		/************************************************************************/
		RenderTarget* mapTarget = (gToggleMSM) ? (pRenderTargetMapMSM) : pRenderTargetMapVSM;
		Pipeline* pPipeline = (gToggleMSM) ? pPipelineMapMSM : pPipelineMapVSM;

		RenderTargetBarrier shadowBarriers[] = {
			{ mapTarget, RESOURCE_STATE_RENDER_TARGET },
			{ pRenderTargetShadowDepth, RESOURCE_STATE_DEPTH_WRITE }
		};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 2, shadowBarriers);

		// Record screen clear
		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
		loadActions.mClearDepth.depth = 1.0f;
		loadActions.mClearDepth.stencil = 0;

		ClearValue clearColor = { { 0.0f, 0.0f, 0.0f, 0.0f} };

		loadActions.mClearColorValues[0] = clearColor;
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;

		cmdBindPipeline(cmd, pPipeline);
		cmdBindRenderTargets(cmd, 1, &mapTarget, pRenderTargetShadowDepth, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)mapTarget->mWidth, (float)mapTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, mapTarget->mWidth, mapTarget->mHeight);
		drawObjects(cmd, "Draw Objects (Shadow Map)", true);

		/************************************************************************/
		// Shadow Blur pass
		/************************************************************************/

		struct ShadowBlurConstant
		{
			uvec2 shadowMapSize;
			bool horizontalPass;
		} shadowConstantData = { gShadowMapData.mSize, true };

		Texture* pTexBlurHor = (gToggleMSM) ? pTexBlurHorMSM : pTexBlurHorVSM;
		Texture* pTexBlurVert = (gToggleMSM) ? pTexBlurVertMSM : pTexBlurVertVSM;

		for (uint32_t blurIndex = 0; blurIndex < gBlurCount; ++blurIndex)
		{
			//// FIRST PASS, HORIZONTAL
			shadowConstantData.horizontalPass = true;

			Texture* src;
			if (blurIndex == 0)
			{
				RenderTargetBarrier blurBarriersHor[] = {
					{ mapTarget, RESOURCE_STATE_SHADER_RESOURCE }
				};
				TextureBarrier texBlurBarrierHor[] = {
					{ pTexBlurHor, RESOURCE_STATE_UNORDERED_ACCESS }
				};
				cmdResourceBarrier(cmd, 0, NULL, 1, texBlurBarrierHor, 1, blurBarriersHor);
				src = mapTarget->pTexture;
			}
			else
			{
				TextureBarrier texBlurBarrierHor[] = {
					{ pTexBlurVert, RESOURCE_STATE_SHADER_RESOURCE },
					{ pTexBlurHor, RESOURCE_STATE_UNORDERED_ACCESS }
				};
				cmdResourceBarrier(cmd, 0, NULL, 2, texBlurBarrierHor, 0, NULL);
				src = pTexBlurVert;
			}


			cmdBindPipeline(cmd, pPipelineShadowBlur[blurIndex][0]);
			cmdBindPushConstants(cmd, pRootSignatureShadowBlur, "RootConstant", &shadowConstantData);
			const uint32_t* pThreadGroupSize = pShaderShadowBlur->pReflection->mStageReflections[0].mNumThreadsPerGroup;
			{
				uint32_t index = gFrameIndex * gMaxBlurs + blurIndex;
				DescriptorData params[2] = {};
				params[0].pName = "srcTexture";
				params[0].ppTextures = &src;
				updateDescriptorSet(pRenderer, index, pDescriptorSetShadowBlur[0], 1, params);

				params[0].pName = "dstTexture";
				params[0].ppTextures = &pTexBlurHor;
				updateDescriptorSet(pRenderer, index, pDescriptorSetShadowBlur[1], 1, params);

				cmdBindDescriptorSet(cmd, index, pDescriptorSetShadowBlur[0]);
				cmdBindDescriptorSet(cmd, index, pDescriptorSetShadowBlur[1]);
			}
			cmdDispatch(cmd,
				gShadowMapData.mSize[0] / pThreadGroupSize[0] + 1,
				gShadowMapData.mSize[1] / pThreadGroupSize[1] + 1,
				1);
			// ---------------------


			////  SECOND PASS, VERTICAL
			shadowConstantData.horizontalPass = false;

			TextureBarrier blurBarriersVert[] = {
				{ pTexBlurHor, RESOURCE_STATE_SHADER_RESOURCE },
				{ pTexBlurVert, RESOURCE_STATE_UNORDERED_ACCESS }
			};
			cmdResourceBarrier(cmd, 0, NULL, 2, blurBarriersVert, 0, NULL);

			cmdBindPushConstants(cmd, pRootSignatureShadowBlur, "RootConstant", &shadowConstantData);
			//Update descriptors for new blur pass, vertical
			{
				uint32_t index = gFrameIndex * gMaxBlurs + gMaxBlurs * gImageCount + blurIndex;

				DescriptorData params[2] = {};
				params[0].pName = "srcTexture";
				params[0].ppTextures = &pTexBlurHor;
				updateDescriptorSet(pRenderer, index, pDescriptorSetShadowBlur[0], 1, params);

				params[0].pName = "dstTexture";
				params[0].ppTextures = &pTexBlurVert;
				updateDescriptorSet(pRenderer, index, pDescriptorSetShadowBlur[1], 1, params);

				cmdBindDescriptorSet(cmd, index, pDescriptorSetShadowBlur[0]);
				cmdBindDescriptorSet(cmd, index, pDescriptorSetShadowBlur[1]);
			}
			cmdBindPipeline(cmd, pPipelineShadowBlur[blurIndex][1]);
			cmdBindPushConstants(cmd, pRootSignatureShadowBlur, "RootConstant", &shadowConstantData);
			cmdDispatch(cmd,
				gShadowMapData.mSize[0] / pThreadGroupSize[0] + 1,
				gShadowMapData.mSize[1] / pThreadGroupSize[1] + 1,
				1);
			////// -------------------------

		}

		/************************************************************************/
		// Main render pass
		/************************************************************************/
		// --------------------------------------
		// Transfer shadow map to a Shader resource state

		if (gBlurCount != 0)
		{
			RenderTargetBarrier rtBarriers[] = {
				{ pRenderTarget, RESOURCE_STATE_RENDER_TARGET },
				{ pRenderTargetDepthBuffer, RESOURCE_STATE_DEPTH_WRITE }
			};
			TextureBarrier texBarriers[] = {
				{ pTexBlurVert, RESOURCE_STATE_SHADER_RESOURCE }
			};
			cmdResourceBarrier(cmd, 0, NULL, 1, texBarriers, 2, rtBarriers);
		}
		else
		{
			RenderTargetBarrier rtBarriers[] = {
				{ pRenderTarget, RESOURCE_STATE_RENDER_TARGET },
				{ mapTarget, RESOURCE_STATE_SHADER_RESOURCE },
				{ pRenderTargetDepthBuffer, RESOURCE_STATE_DEPTH_WRITE }
			};
			cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 3, rtBarriers);
		}


		clearColor.r = 0.15f;
		clearColor.g = 0.15f;
		clearColor.b = 0.15f;
		clearColor.a = 1.0f;

		loadActions.mClearColorValues[0] = clearColor;

		pPipeline = (gToggleMSM) ? pPipelineMSM : pPipelineVSM;
		RootSignature* pRootSignature = (gToggleMSM) ? pRootSignatureMSM : pRootSignatureVSM;

		cmdBindPipeline(cmd, pPipeline);
		cmdBindPushConstants(cmd, pRootSignature, "cbShadowRootConstants", &shadowConstantData);
		{
			DescriptorData params[1] = {};
			params[0].pName = "shadowMap";
			params[0].ppTextures = (gBlurCount) ? &pTexBlurVert : &mapTarget->pTexture;

			DescriptorSet* pDescriptorSet = (gToggleMSM) ? pDescriptorSetMSM[1] : pDescriptorSetVSM[1];

			updateDescriptorSet(pRenderer, gFrameIndex, pDescriptorSet, 1, params);
			cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSet);
		}

		cmdBindRenderTargets(cmd, 1, &pRenderTarget, pRenderTargetDepthBuffer, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);
		drawObjects(cmd, "Draw Objects", false);


		/************************************************************************/
		// Draw UI
		/************************************************************************/
		loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

		gVirtualJoystick.Draw(cmd, { 1.0f, 1.0f, 1.0f, 1.0f });

		const float txtIndent = 8.f;
		float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(txtIndent, 15.f), &gFrameTimeDraw);
		cmdDrawGpuProfile(cmd, float2(txtIndent, txtSizePx.y + 30.f), gGpuProfileToken, &gFrameTimeDraw);



		cmdDrawProfilerUI();

		gAppUI.Gui(pGui);
		gAppUI.Draw(cmd);
		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		RenderTargetBarrier present = { pRenderTarget, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, &present);

		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &pSemaphoreRenderComplete;
		submitDesc.ppWaitSemaphores = &pSemaphoreImageAcquired;
		submitDesc.pSignalFence = pFenceRenderComplete;
		queueSubmit(pGraphicsQueue, &submitDesc);
		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.ppWaitSemaphores = &pSemaphoreRenderComplete;
		presentDesc.mSubmitDone = true;
		queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gImageCount;

	}

	const char* GetName() { return "09b_MomentShadows"; }

private:

	vec3 SphericalToCartesian(const vec3& coord)
	{
		float radius = coord[0];
		float theta = Vectormath::degToRad(coord[1]);
		float phi = Vectormath::degToRad(coord[2]);

		float a = radius * cosf(theta);

		vec3 result;
		result[0] = a * cosf(phi);
		result[1] = radius * sinf(theta);
		result[2] = a * sinf(phi);

		return result;
	}

	float RandomZeroOne()
	{
		return ((float)rand() / RAND_MAX);
	}

	void PrepareSphere(vec3& translate, uint32_t i)
	{
		mat4 sphereMat = mat4::identity();
		// Start at a random timer
		gSphereTimers[i] = RandomZeroOne() * 100.0f;

		gSphereBounceModifiers[i] = RandomZeroOne() + 0.5f;

		gDataSphere[i].mWorld = sphereMat.translation(translate) * sphereMat.scale(vec3(RandomZeroOne() * 0.3f + 0.2f));
		gDataSphere[i].mDiffuse = { RandomZeroOne(), RandomZeroOne(), RandomZeroOne(), 1.0f };
		gDataSphere[i].mSpecular = vec4(vec3(RandomZeroOne() * 0.03f), RandomZeroOne() * 24.0f);
		BufferUpdateDesc sphereDataUpdateDesc = { pBufferUniformSphere[i] };
		beginUpdateResource(&sphereDataUpdateDesc);
		*(UniformObjectData*)sphereDataUpdateDesc.pMappedData = gDataSphere[i];
		endUpdateResource(&sphereDataUpdateDesc, NULL);
	}

	void PrepareResources()
	{
		// Set spheres
		// Horizontal line
		for (int i = 0; i < 9; ++i)
		{
			//float negOneToOne = RandomZeroOne() * 2.0f - 1.0f;
			vec3 translate = { i - 4.0f, 0.0f, 0.0f };
			PrepareSphere(translate, i);
		}

		// Vert left (eliminate center one as it was created above)
		for (int i = 9, j = 0; i < 19; ++i, ++j)
		{
			vec3 translate = { -4.0, 0.0f, 5.0f - j };

			// just knock it right out of here lol
			if (i == 14)
			{
				translate[0] -= 999999.0f;
			}
			PrepareSphere(translate, i);
		}

		// Vert right (eliminate center one as it was created above)
		for (int i = 19, j = 0; i < 29; ++i, ++j)
		{
			vec3 translate = { 4.0, 0.0f, 5.0f - j };

			if (i == 24) {
				translate[0] -= 999999.f;
			}
			PrepareSphere(translate, i);
		}

		// Set plane
		mat4 planeMat = mat4::identity();
		gDataPlane.mWorld = planeMat.setTranslation(gPlanePosition);
		gDataPlane.mDiffuse = { 0.65f, 0.65f, 0.65f, 1.0f };
		gDataPlane.mSpecular = vec4(gMiniSpec, 2.0f);
		BufferUpdateDesc planeDataUpdateDesc = { pBufferUniformPlane };
		beginUpdateResource(&planeDataUpdateDesc);
		*(UniformObjectData*)planeDataUpdateDesc.pMappedData = gDataPlane;
		endUpdateResource(&planeDataUpdateDesc, NULL);

	}

	void PrepareDescriptorSets()
	{

		/************************************************************************/
		// Shadow pass descriptors
		/************************************************************************/
		{
			DescriptorData params[2] = {};
			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				params[0].pName = "cbLight";
				params[0].ppBuffers = &pBufferUniformLight[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetMapVSM[0], 1, params);
			}

			for (uint32_t i = 0; i < gNumSpheres; ++i)
			{
				params[0] = {};
				params[0].pName = "cbObject";
				params[0].ppBuffers = &pBufferUniformSphere[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetMapVSM[1], 1, params);
			}
			params[0].ppBuffers = &pBufferUniformPlane;
			updateDescriptorSet(pRenderer, gNumSpheres, pDescriptorSetMapVSM[1], 1, params);

			params[0].ppBuffers = &pBufferUniformLightObject;
			updateDescriptorSet(pRenderer, gNumSpheres + 1, pDescriptorSetMapVSM[1], 1, params);

		}

		{
			DescriptorData params[2] = {};
			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				params[0].pName = "cbLight";
				params[0].ppBuffers = &pBufferUniformLight[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetMapMSM[0], 1, params);
			}

			for (uint32_t i = 0; i < gNumSpheres; ++i)
			{
				params[0] = {};
				params[0].pName = "cbObject";
				params[0].ppBuffers = &pBufferUniformSphere[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetMapMSM[1], 1, params);
			}
			params[0].ppBuffers = &pBufferUniformPlane;
			updateDescriptorSet(pRenderer, gNumSpheres, pDescriptorSetMapMSM[1], 1, params);

			params[0].ppBuffers = &pBufferUniformLightObject;
			updateDescriptorSet(pRenderer, gNumSpheres + 1, pDescriptorSetMapMSM[1], 1, params);

		}



		/************************************************************************/
		// VSM descriptors
		/************************************************************************/
		{
			DescriptorData params[3] = {};
			
			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				params[0] = {};
				params[0].pName = "cbCamera";
				params[0].ppBuffers = &pBufferUniformCamera[i];

				params[1] = {};
				params[1].pName = "cbLight";
				params[1].ppBuffers = &pBufferUniformLight[i];

				updateDescriptorSet(pRenderer, i, pDescriptorSetVSM[1], 2, params);
			}

			for (uint32_t i = 0; i < gNumSpheres; ++i)
			{
				params[0] = {};
				params[0].pName = "cbObject";
				params[0].ppBuffers = &pBufferUniformSphere[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetVSM[2], 1, params);
			}
			params[0].ppBuffers = &pBufferUniformPlane;
			updateDescriptorSet(pRenderer, gNumSpheres, pDescriptorSetVSM[2], 1, params);

			params[0].ppBuffers = &pBufferUniformLightObject;
			updateDescriptorSet(pRenderer, gNumSpheres + 1, pDescriptorSetVSM[2], 1, params);

		}

		/************************************************************************/
		// MSM descriptors
		/************************************************************************/
		{
			DescriptorData params[3] = {};
			
			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				params[0] = {};
				params[0].pName = "cbCamera";
				params[0].ppBuffers = &pBufferUniformCamera[i];

				params[1] = {};
				params[1].pName = "cbLight";
				params[1].ppBuffers = &pBufferUniformLight[i];

				updateDescriptorSet(pRenderer, i, pDescriptorSetMSM[1], 2, params);
			}
			for (uint32_t i = 0; i < gNumSpheres; ++i)
			{
				params[0] = {};
				params[0].pName = "cbObject";
				params[0].ppBuffers = &pBufferUniformSphere[i];
				updateDescriptorSet(pRenderer, i, pDescriptorSetMSM[2], 1, params);
			}
			params[0].ppBuffers = &pBufferUniformPlane;
			updateDescriptorSet(pRenderer, gNumSpheres, pDescriptorSetMSM[2], 1, params);

			params[0].ppBuffers = &pBufferUniformLightObject;
			updateDescriptorSet(pRenderer, gNumSpheres + 1, pDescriptorSetMSM[2], 1, params);

		}

	}

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = gImageCount;
		swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true);
		swapChainDesc.mEnableVsync = mSettings.mDefaultVSyncEnabled;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	bool addRenderTargets()
	{
		// Add depth buffer
		RenderTargetDesc depthRT = {};
		depthRT.mArraySize = 1;
		depthRT.mClearValue.depth = 1.0f;
		depthRT.mClearValue.stencil = 0;
		depthRT.mDepth = 1;
		depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
		depthRT.mHeight = mSettings.mHeight;
		depthRT.mSampleCount = SAMPLE_COUNT_1;
		depthRT.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		depthRT.mSampleQuality = 0;
		depthRT.mWidth = mSettings.mWidth;
		depthRT.mFlags = TEXTURE_CREATION_FLAG_ESRAM;
		depthRT.pName = "Depth RT";
		addRenderTarget(pRenderer, &depthRT, &pRenderTargetDepthBuffer);

		/************************************************************************/
		// Shadow Map Render target
		/************************************************************************/
		RenderTargetDesc VSMRenderTargetDesc = {};
		VSMRenderTargetDesc.mArraySize = 1;
		VSMRenderTargetDesc.mDepth = 1;
		VSMRenderTargetDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		VSMRenderTargetDesc.mFormat = TinyImageFormat_R32G32_SFLOAT;
		VSMRenderTargetDesc.mWidth = gShadowMapData.mSize[0];
		VSMRenderTargetDesc.mHeight = gShadowMapData.mSize[1];
		VSMRenderTargetDesc.mSampleCount = (SampleCount)1;
		VSMRenderTargetDesc.mSampleQuality = 0;
		VSMRenderTargetDesc.pName = "VSM RT";
		addRenderTarget(pRenderer, &VSMRenderTargetDesc, &pRenderTargetMapVSM);

		RenderTargetDesc MSMRenderTargetDesc = VSMRenderTargetDesc;
		MSMRenderTargetDesc.mFormat = TinyImageFormat_R16G16B16A16_UNORM;
		MSMRenderTargetDesc.pName = "MSM RT";
		addRenderTarget(pRenderer, &MSMRenderTargetDesc, &pRenderTargetMapMSM);


		RenderTargetDesc shadowDepthRTDesc = {};
		shadowDepthRTDesc.mArraySize = 1;
		shadowDepthRTDesc.mClearValue.depth = 1.0f;
		shadowDepthRTDesc.mDepth = 1;
		shadowDepthRTDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		shadowDepthRTDesc.mFormat = TinyImageFormat_D32_SFLOAT;
		shadowDepthRTDesc.mWidth = gShadowMapData.mSize[0];
		shadowDepthRTDesc.mHeight = gShadowMapData.mSize[1];
		shadowDepthRTDesc.mSampleCount = (SampleCount)1;
		shadowDepthRTDesc.mSampleQuality = 0;
		shadowDepthRTDesc.pName = "Shadow Map Depth RT";
		addRenderTarget(pRenderer, &shadowDepthRTDesc, &pRenderTargetShadowDepth);


		/************************************************************************/
		// Shadow Blur Render Targets
		/************************************************************************/
		TextureLoadDesc blurLoadDesc = {};
		TextureDesc     blurDesc = {};
		blurDesc.mWidth = gShadowMapData.mSize[0];
		blurDesc.mHeight = gShadowMapData.mSize[1];
		blurDesc.mDepth = 1;
		blurDesc.mArraySize = 1;
		blurDesc.mMipLevels = 1;
		blurDesc.mFormat = TinyImageFormat_R32G32_SFLOAT;
		blurDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE | DESCRIPTOR_TYPE_RW_TEXTURE;
		blurDesc.mSampleCount = SAMPLE_COUNT_1;
		blurDesc.mHostVisible = false;
		blurLoadDesc.pDesc = &blurDesc;


		blurDesc.pName = "VSM Horizontal Blur";
		blurLoadDesc.ppTexture = &pTexBlurHorVSM;
		addResource(&blurLoadDesc, NULL);

		blurDesc.pName = "VSM Vertical Blur";
		blurLoadDesc.ppTexture = &pTexBlurVertVSM;
		addResource(&blurLoadDesc, NULL);

		blurDesc.mFormat = TinyImageFormat_R16G16B16A16_UNORM;
		blurDesc.pName = "MSM Horizontal Blur";
		blurLoadDesc.ppTexture = &pTexBlurHorMSM;
		addResource(&blurLoadDesc, NULL);
		blurDesc.pName = "MSM Vertical Blur";
		blurLoadDesc.ppTexture = &pTexBlurVertMSM;
		addResource(&blurLoadDesc, NULL);



		return (pRenderTargetDepthBuffer != NULL) &&
			(pRenderTargetMapVSM != NULL) &&
			(pRenderTargetMapMSM != NULL) &&
			(pRenderTargetShadowDepth != NULL) &&
			(pTexBlurHorVSM != NULL) &&
			(pTexBlurVertVSM != NULL) &&
			(pTexBlurHorMSM != NULL) &&
			(pTexBlurVertMSM != NULL)

			;
	}

	void drawObjects(Cmd* cmd, const char* profilerName, bool shadowPass)
	{
		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, profilerName);

		DescriptorSet** set;
		if (shadowPass)
			if (gToggleMSM)
				set = &pDescriptorSetMapMSM[0];
			else
				set = &pDescriptorSetMapVSM[0];
		else
			if (gToggleMSM)
				set = &pDescriptorSetMSM[0];
			else
				set = &pDescriptorSetVSM[0];


		uint32_t accessIndex = 0;

		// Bind depth texture if not shadow pass
		if (!shadowPass)
		{
			cmdBindDescriptorSet(cmd, 0, set[accessIndex++]);
		}

		// Bind camera & lights
		cmdBindDescriptorSet(cmd, gFrameIndex, set[accessIndex++]);


		// OBJECTS
		// -----------------

		// Draw Sphere
		const uint32_t vbSphereStride = sizeof(float) * 6;
		cmdBindVertexBuffer(cmd, 1, &pBufferVertexSphere, &vbSphereStride, NULL);
		for (int i = 0; i < gNumSpheres; ++i)
		{
			cmdBindDescriptorSet(cmd, i, set[accessIndex]);
			cmdDraw(cmd, gNumberOfSpherePoints / 6, 0);
		}

		// Draw Plane
		const uint32_t vbPlaneStride = sizeof(float) * 6;
		cmdBindVertexBuffer(cmd, 1, &pBufferVertexPlane, &vbPlaneStride, NULL);
		cmdBindDescriptorSet(cmd, gNumSpheres, set[accessIndex]);
		cmdDraw(cmd, gNumberOfPlanePoints / 6, 0);

		// Draw Light Object
		if (!shadowPass)
		{
			const uint32_t vbLightObjectStride = sizeof(float) * 6;
			cmdBindVertexBuffer(cmd, 1, &pBufferVertexLightObject, &vbLightObjectStride, NULL);
			cmdBindDescriptorSet(cmd, gNumSpheres + 1, set[accessIndex]);
			cmdDraw(cmd, gNumberOfLightObjectPoints / 6, 0);
		}


		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);
		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
	}

};

DEFINE_APPLICATION_MAIN(MomentShadows)
