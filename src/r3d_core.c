﻿/*
 * Copyright (c) 2025 Le Juez Victor
 *
 * This software is provided "as-is", without any express or implied warranty. In no event
 * will the authors be held liable for any damages arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose, including commercial
 * applications, and to alter it and redistribute it freely, subject to the following restrictions:
 *
 *   1. The origin of this software must not be misrepresented; you must not claim that you
 *   wrote the original software. If you use this software in a product, an acknowledgment
 *   in the product documentation would be appreciated but is not required.
 *
 *   2. Altered source versions must be plainly marked as such, and must not be misrepresented
 *   as being the original software.
 *
 *   3. This notice may not be removed or altered from any source distribution.
 */

#include "details/r3d_frustum.h"
#include "r3d.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <glad.h>

#include <assimp/cimport.h>
#include <float.h>

#include "./r3d_state.h"
#include "./details/r3d_light.h"
#include "./details/r3d_drawcall.h"
#include "./details/r3d_billboard.h"
#include "./details/r3d_primitives.h"
#include "./details/containers/r3d_array.h"
#include "./details/containers/r3d_registry.h"

/* === Internal declarations === */

static bool r3d_has_deferred_calls(void);
static bool r3d_has_forward_calls(void);

static void r3d_sprite_get_uv_scale_offset(Vector2* uvScale, Vector2* uvOffset, const R3D_Sprite* sprite, float sgnX, float sgnY);

static void r3d_depth_stencil_attach(void);
static void r3d_stencil_enable_geometry_write(void);
static void r3d_stencil_enable_geometry_test(GLenum condition);
static void r3d_stencil_enable_effect_write(uint8_t effectID);
static void r3d_stencil_enable_effect_test(GLenum condition, uint8_t effectID);
static void r3d_stencil_enable_effect_write_with_geometry_test(GLenum condition, uint8_t effectID);
static void r3d_stencil_disable(void);

static void r3d_prepare_process_lights_and_batch(void);
static void r3d_prepare_cull_drawcalls(void);
static void r3d_prepare_sort_drawcalls(void);
static void r3d_prepare_anim_drawcalls(void);

static void r3d_clear_gbuffer(bool bindFramebuffer, bool clearColor, bool clearDepth, bool clearStencil);

static void r3d_pass_shadow_maps(void);
static void r3d_pass_gbuffer(void);
static void r3d_pass_ssao(void);

static void r3d_pass_deferred_ambient(void);
static void r3d_pass_deferred_lights(void);

static void r3d_pass_scene_background(void);
static void r3d_pass_scene_deferred(void);
static void r3d_pass_scene_forward_depth_prepass(void);
static void r3d_pass_scene_forward(void);

static void r3d_pass_post_setup(void);
static void r3d_pass_post_bloom(void);
static void r3d_pass_post_fog(void);
static void r3d_pass_post_output(void);
static void r3d_pass_post_fxaa(void);

static void r3d_pass_final_blit(void);

static void r3d_reset_raylib_state(void);

/* === Public functions === */

void R3D_Init(int resWidth, int resHeight, unsigned int flags)
{
    // Set parameter flags
    R3D.state.flags = flags;

    // Check GPU supports
    r3d_support_check_texture_internal_formats();

    // Load draw call arrays
    R3D.container.aDrawForward = r3d_array_create(128, sizeof(r3d_drawcall_t));
    R3D.container.aDrawDeferred = r3d_array_create(128, sizeof(r3d_drawcall_t));
    R3D.container.aDrawForwardInst = r3d_array_create(8, sizeof(r3d_drawcall_t));
    R3D.container.aDrawDeferredInst = r3d_array_create(8, sizeof(r3d_drawcall_t));

    // Load lights registry
    R3D.container.rLights = r3d_registry_create(8, sizeof(r3d_light_t));
    R3D.container.aLightBatch = r3d_array_create(8, sizeof(r3d_light_batched_t));

    // Environment data
    R3D.env.backgroundColor = (Vector3) { 0.2f, 0.2f, 0.2f };
    R3D.env.ambientColor = (Vector3) { 0.2f, 0.2f, 0.2f };
    R3D.env.quatSky = QuaternionIdentity();
    R3D.env.useSky = false;
    R3D.env.ssaoEnabled = false;
    R3D.env.ssaoRadius = 0.5f;
    R3D.env.ssaoBias = 0.025f;
    R3D.env.ssaoIterations = 10;
    R3D.env.bloomMode = R3D_BLOOM_DISABLED;
    R3D.env.bloomIntensity = 0.05f;
    R3D.env.bloomFilterRadius = 0;
    R3D.env.bloomThreshold = 0.0f;
    R3D.env.bloomSoftThreshold = 0.5f;
    R3D.env.fogMode = R3D_FOG_DISABLED;
    R3D.env.fogColor = (Vector3) { 1.0f, 1.0f, 1.0f };
    R3D.env.fogStart = 1.0f;
    R3D.env.fogEnd = 50.0f;
    R3D.env.fogDensity = 0.05f;
    R3D.env.tonemapMode = R3D_TONEMAP_LINEAR;
    R3D.env.tonemapExposure = 1.0f;
    R3D.env.tonemapWhite = 1.0f;
    R3D.env.brightness = 1.0f;
    R3D.env.contrast = 1.0f;
    R3D.env.saturation = 1.0f;

    // Init resolution state
    R3D.state.resolution.width = resWidth;
    R3D.state.resolution.height = resHeight;
    R3D.state.resolution.texelX = 1.0f / resWidth;
    R3D.state.resolution.texelY = 1.0f / resHeight;

    // Init scene data
    R3D.state.scene.bounds = (BoundingBox) {
        (Vector3) { -100, -100, -100 },
        (Vector3) {  100,  100,  100 }
    };

    // Init default loading parameters
    R3D.state.loading.aiProps = aiCreatePropertyStore();
    R3D.state.loading.textureFilter = TEXTURE_FILTER_TRILINEAR;

    // Load primitive shapes
    glGenVertexArrays(1, &R3D.primitive.dummyVAO);
    R3D.primitive.quad = r3d_primitive_load_quad();
    R3D.primitive.cube = r3d_primitive_load_cube();

    // Init misc data
    R3D.misc.matCubeViews[0] = MatrixLookAt((Vector3) { 0 }, (Vector3) {  1.0f,  0.0f,  0.0f }, (Vector3) { 0.0f, -1.0f,  0.0f });
    R3D.misc.matCubeViews[1] = MatrixLookAt((Vector3) { 0 }, (Vector3) { -1.0f,  0.0f,  0.0f }, (Vector3) { 0.0f, -1.0f,  0.0f });
    R3D.misc.matCubeViews[2] = MatrixLookAt((Vector3) { 0 }, (Vector3) {  0.0f,  1.0f,  0.0f }, (Vector3) { 0.0f,  0.0f,  1.0f });
    R3D.misc.matCubeViews[3] = MatrixLookAt((Vector3) { 0 }, (Vector3) {  0.0f, -1.0f,  0.0f }, (Vector3) { 0.0f,  0.0f, -1.0f });
    R3D.misc.matCubeViews[4] = MatrixLookAt((Vector3) { 0 }, (Vector3) {  0.0f,  0.0f,  1.0f }, (Vector3) { 0.0f, -1.0f,  0.0f });
    R3D.misc.matCubeViews[5] = MatrixLookAt((Vector3) { 0 }, (Vector3) {  0.0f,  0.0f, -1.0f }, (Vector3) { 0.0f, -1.0f,  0.0f });

    // Load GL Objects - framebuffers, textures, shaders...
    // NOTE: The initialization of these resources is based
    //       on the global state and should be performed last.
    r3d_framebuffers_load(resWidth, resHeight);
    r3d_textures_load();
    r3d_shaders_load();

    // Defines suitable clipping plane distances for r3d
    rlSetClipPlanes(0.05f, 4000.0f);
}

void R3D_Close(void)
{
    aiReleasePropertyStore(R3D.state.loading.aiProps);

    r3d_framebuffers_unload();
    r3d_textures_unload();
    r3d_shaders_unload();

    r3d_array_destroy(&R3D.container.aDrawForward);
    r3d_array_destroy(&R3D.container.aDrawDeferred);
    r3d_array_destroy(&R3D.container.aDrawForwardInst);
    r3d_array_destroy(&R3D.container.aDrawDeferredInst);

    r3d_registry_destroy(&R3D.container.rLights);
    r3d_array_destroy(&R3D.container.aLightBatch);

    glDeleteVertexArrays(1, &R3D.primitive.dummyVAO);
    r3d_primitive_unload(&R3D.primitive.quad);
    r3d_primitive_unload(&R3D.primitive.cube);
}

bool R3D_HasState(unsigned int flag)
{
    return R3D.state.flags & flag;
}

void R3D_SetState(unsigned int flags)
{
    if (flags & R3D_FLAG_8_BIT_NORMALS) {
        TraceLog(LOG_WARNING, "R3D: Cannot set 'R3D_FLAG_8_BIT_NORMALS'; this flag must be set during R3D initialization");
        flags &= ~R3D_FLAG_8_BIT_NORMALS;
    }

    if (flags & R3D_FLAG_LOW_PRECISION_BUFFERS) {
        TraceLog(LOG_WARNING, "R3D: Cannot set 'R3D_FLAG_LOW_PRECISION_BUFFERS'; this flag must be set during R3D initialization");
        flags &= ~R3D_FLAG_LOW_PRECISION_BUFFERS;
    }

    R3D.state.flags |= flags;

    if (flags & R3D_FLAG_FXAA) {
        if (R3D.shader.screen.fxaa.id == 0) {
            r3d_shader_load_screen_fxaa();
        }
    }
}

void R3D_ClearState(unsigned int flags)
{
    if (flags & R3D_FLAG_8_BIT_NORMALS) {
        TraceLog(LOG_WARNING, "R3D: Cannot clear 'R3D_FLAG_8_BIT_NORMALS'; this flag must be set during R3D initialization");
        flags &= ~R3D_FLAG_8_BIT_NORMALS;
    }

    if (flags & R3D_FLAG_LOW_PRECISION_BUFFERS) {
        TraceLog(LOG_WARNING, "R3D: Cannot clear 'R3D_FLAG_LOW_PRECISION_BUFFERS'; this flag must be set during R3D initialization");
        flags &= ~R3D_FLAG_LOW_PRECISION_BUFFERS;
    }

    R3D.state.flags &= ~flags;
}

void R3D_GetResolution(int* width, int* height)
{
    if (width) *width = R3D.state.resolution.width;
    if (height) *height = R3D.state.resolution.height;
}

void R3D_UpdateResolution(int width, int height)
{
    if (width <= 0 || height <= 0) {
        TraceLog(LOG_ERROR, "R3D: Invalid resolution given to 'R3D_UpdateResolution'");
        return;
    }

    if (width == R3D.state.resolution.width && height == R3D.state.resolution.height) {
        return;
    }

    r3d_framebuffers_unload();
    r3d_framebuffers_load(width, height);

    R3D.state.resolution.width = width;
    R3D.state.resolution.height = height;
    R3D.state.resolution.texelX = 1.0f / width;
    R3D.state.resolution.texelY = 1.0f / height;
}

void R3D_SetRenderTarget(const RenderTexture* target)
{
    if (target == NULL) {
        memset(&R3D.framebuffer.customTarget, 0, sizeof(RenderTexture));
        return;
    }

    R3D.framebuffer.customTarget = *target;
}

void R3D_SetSceneBounds(BoundingBox sceneBounds)
{
    R3D.state.scene.bounds = sceneBounds;
}

void R3D_SetTextureFilter(TextureFilter filter)
{
    R3D.state.loading.textureFilter = filter;
}

void R3D_Begin(Camera3D camera)
{
    // Render the batch before proceeding
    rlDrawRenderBatchActive();

    // Clear the previous draw call array state
    r3d_array_clear(&R3D.container.aDrawForward);
    r3d_array_clear(&R3D.container.aDrawDeferred);
    r3d_array_clear(&R3D.container.aDrawForwardInst);
    r3d_array_clear(&R3D.container.aDrawDeferredInst);

    // Store camera position
    R3D.state.transform.viewPos = camera.position;

    // Compute aspect ratio
    float aspect = 1.0f;
    if (R3D.state.flags & R3D_FLAG_ASPECT_KEEP) {
        aspect = (float)R3D.state.resolution.width / R3D.state.resolution.height;
    }
    else {
        aspect = (float)GetScreenWidth() / GetScreenHeight();
    }

    // Compute projection matrix
    if (camera.projection == CAMERA_PERSPECTIVE) {
        double top = rlGetCullDistanceNear() * tan(camera.fovy * 0.5 * DEG2RAD);
        double right = top * aspect;
        R3D.state.transform.proj = MatrixFrustum(
            -right, right, -top, top,
            rlGetCullDistanceNear(),
            rlGetCullDistanceFar()
        );
    }
    else if (camera.projection == CAMERA_ORTHOGRAPHIC) {
        double top = camera.fovy / 2.0;
        double right = top * aspect;
        R3D.state.transform.proj = MatrixOrtho(
            -right, right, -top, top,
            rlGetCullDistanceNear(),
            rlGetCullDistanceFar()
        );
    }

    // Compute view matrix
    R3D.state.transform.view = MatrixLookAt(camera.position, camera.target, camera.up);

    // Store inverse matrices
    R3D.state.transform.invProj = MatrixInvert(R3D.state.transform.proj);
    R3D.state.transform.invView = MatrixInvert(R3D.state.transform.view);

    // Compute view projection matrix
    R3D.state.transform.viewProj = MatrixMultiply(R3D.state.transform.view, R3D.state.transform.proj);

    // Compute frustum
    R3D.state.frustum.aabb = r3d_frustum_get_bounding_box(R3D.state.transform.viewProj);
    R3D.state.frustum.shape = r3d_frustum_create(R3D.state.transform.viewProj);
}

void R3D_End(void)
{
    /* --- Rendering in shadow maps --- */

    r3d_prepare_process_lights_and_batch();
    r3d_pass_shadow_maps();

    /* --- Prcoess all draw calls before rendering --- */

    if (!(R3D.state.flags & R3D_FLAG_NO_FRUSTUM_CULLING)) {
        r3d_prepare_cull_drawcalls();
    }

    r3d_prepare_sort_drawcalls();
    r3d_prepare_anim_drawcalls();

    /* --- Rasterizing Geometries in G-Buffer --- */

    if (r3d_has_deferred_calls()) {
        r3d_pass_gbuffer(); //< This pass also clear the gbuffer...
    }
    else {
        r3d_clear_gbuffer(true, false, true, true);
    }

    /* --- Calculates ambient occlusion for opaque objects --- */

    if (R3D.env.ssaoEnabled) {
        r3d_pass_ssao();
    }

    /* --- Accumulation of deferred lighting --- */

    if (r3d_has_deferred_calls()) {
        r3d_pass_deferred_ambient();
        r3d_pass_deferred_lights();
    }

    /* --- Final rendering of the scene --- */

    r3d_pass_scene_background();

    if (r3d_has_deferred_calls()) {
        r3d_pass_scene_deferred();
    }

    if (r3d_has_forward_calls()) {
        if (R3D.state.flags & R3D_FLAG_DEPTH_PREPASS) {
            r3d_pass_scene_forward_depth_prepass();
        }
        r3d_pass_scene_forward();
    }

    /* --- Applying effects over the scene and final blit --- */

    r3d_pass_post_setup();

    if (R3D.env.bloomMode != R3D_BLOOM_DISABLED) {
        r3d_pass_post_bloom();
    }

    if (R3D.env.fogMode != R3D_FOG_DISABLED) {
        r3d_pass_post_fog();
    }

    r3d_pass_post_output();

    if (R3D.state.flags & R3D_FLAG_FXAA) {
        r3d_pass_post_fxaa();
    }

    r3d_pass_final_blit();

    /* --- Reset states changed by R3D --- */

    r3d_reset_raylib_state();
}

void R3D_DrawMesh(const R3D_Mesh* mesh, const R3D_Material* material, Matrix transform)
{
    r3d_drawcall_t drawCall = { 0 };

    if (mesh == NULL) return;

    switch (material->billboardMode) {
    case R3D_BILLBOARD_FRONT:
        r3d_transform_to_billboard_front(&transform, &R3D.state.transform.invView);
        break;
    case R3D_BILLBOARD_Y_AXIS:
        r3d_transform_to_billboard_y(&transform, &R3D.state.transform.invView);
        break;
    default:
        break;
    }

    drawCall.transform = transform;
    drawCall.material = material ? *material : R3D_GetDefaultMaterial();
    drawCall.geometry.model.mesh = mesh;
    drawCall.geometryType = R3D_DRAWCALL_GEOMETRY_MODEL;
    drawCall.renderMode = R3D_DRAWCALL_RENDER_DEFERRED;

    r3d_array_t* arr = &R3D.container.aDrawDeferred;
    if (material->blendMode != R3D_BLEND_OPAQUE || R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
        drawCall.renderMode = R3D_DRAWCALL_RENDER_FORWARD;
        arr = &R3D.container.aDrawForward;
    }

    r3d_array_push_back(arr, &drawCall);
}

void R3D_DrawMeshInstanced(const R3D_Mesh* mesh, const R3D_Material* material, const Matrix* instanceTransforms, int instanceCount)
{
    R3D_DrawMeshInstancedPro(mesh, material, NULL, MatrixIdentity(), instanceTransforms, 0, NULL, 0, instanceCount);
}

void R3D_DrawMeshInstancedEx(const R3D_Mesh* mesh, const R3D_Material* material, const Matrix* instanceTransforms, const Color* instanceColors, int instanceCount)
{
    R3D_DrawMeshInstancedPro(mesh, material, NULL, MatrixIdentity(), instanceTransforms, 0, instanceColors, 0, instanceCount);
}

void R3D_DrawMeshInstancedPro(const R3D_Mesh* mesh, const R3D_Material* material,
                              const BoundingBox* globalAabb, Matrix globalTransform,
                              const Matrix* instanceTransforms, int transformsStride,
                              const Color* instanceColors, int colorsStride,
                              int instanceCount)
{
    r3d_drawcall_t drawCall = { 0 };

    if (mesh == NULL || instanceCount == 0 || instanceTransforms == NULL) {
        return;
    }

    drawCall.transform = globalTransform;
    drawCall.material = material ? *material : R3D_GetDefaultMaterial();
    drawCall.geometry.model.mesh = mesh;
    drawCall.geometryType = R3D_DRAWCALL_GEOMETRY_MODEL;
    drawCall.renderMode = R3D_DRAWCALL_RENDER_DEFERRED;

    drawCall.instanced.allAabb = globalAabb ? *globalAabb
        : (BoundingBox) {
            { -FLT_MAX, -FLT_MAX, -FLT_MAX },
            { +FLT_MAX, +FLT_MAX, +FLT_MAX }
        };

    drawCall.instanced.transforms = instanceTransforms;
    drawCall.instanced.transStride = transformsStride;
    drawCall.instanced.colStride = colorsStride;
    drawCall.instanced.colors = instanceColors;
    drawCall.instanced.count = instanceCount;

    r3d_array_t* arr = &R3D.container.aDrawDeferredInst;
    if (material->blendMode != R3D_BLEND_OPAQUE || R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
        drawCall.renderMode = R3D_DRAWCALL_RENDER_FORWARD;
        arr = &R3D.container.aDrawForwardInst;
    }

    r3d_array_push_back(arr, &drawCall);
}

void R3D_DrawModel(const R3D_Model* model, Vector3 position, float scale)
{
    Vector3 vScale = { scale, scale, scale };
    Vector3 rotationAxis = { 0.0f, 1.0f, 0.0f };
    R3D_DrawModelEx(model, position, rotationAxis, 0.0f, vScale);
}

void R3D_DrawModelEx(const R3D_Model* model, Vector3 position, Vector3 rotationAxis, float rotationAngle, Vector3 scale)
{
    Matrix matScale = MatrixScale(scale.x, scale.y, scale.z);
    Matrix matRotation = MatrixRotate(rotationAxis, rotationAngle * DEG2RAD);
    Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);
    Matrix matTransform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);

    R3D_DrawModelPro(model, matTransform);
}

void R3D_DrawModelPro(const R3D_Model* model, Matrix transform)
{
    if (model == NULL) return;

    for (int i = 0; i < model->meshCount; i++)
    {
        const R3D_Material* material = &model->materials[model->meshMaterials[i]];
        const R3D_Mesh* mesh = &model->meshes[i];

        r3d_drawcall_t drawCall = { 0 };

        if (mesh == NULL) return;

        switch (material->billboardMode) {
        case R3D_BILLBOARD_FRONT:
            r3d_transform_to_billboard_front(&transform, &R3D.state.transform.invView);
            break;
        case R3D_BILLBOARD_Y_AXIS:
            r3d_transform_to_billboard_y(&transform, &R3D.state.transform.invView);
            break;
        default:
            break;
        }

        drawCall.transform = transform;
        drawCall.material = material ? *material : R3D_GetDefaultMaterial();
        drawCall.geometry.model.mesh = mesh;
        drawCall.geometryType = R3D_DRAWCALL_GEOMETRY_MODEL;
        drawCall.renderMode = R3D_DRAWCALL_RENDER_DEFERRED;

        drawCall.geometry.model.anim = model->anim;
        drawCall.geometry.model.frame = model->animFrame;
        drawCall.geometry.model.boneOffsets = model->boneOffsets;

        r3d_array_t* arr = &R3D.container.aDrawDeferred;
        if (material->blendMode != R3D_BLEND_OPAQUE || R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
            drawCall.renderMode = R3D_DRAWCALL_RENDER_FORWARD;
            arr = &R3D.container.aDrawForward;
        }

        r3d_array_push_back(arr, &drawCall);
    }
}

void R3D_DrawSprite(const R3D_Sprite* sprite, Vector3 position)
{
    R3D_DrawSpritePro(sprite, position, (Vector2) { 1.0f, 1.0f }, (Vector3) { 0, 1, 0 }, 0.0f);
}

void R3D_DrawSpriteEx(const R3D_Sprite* sprite, Vector3 position, Vector2 size, float rotation)
{
    R3D_DrawSpritePro(sprite, position, size, (Vector3) { 0, 1, 0 }, rotation);
}

void R3D_DrawSpritePro(const R3D_Sprite* sprite, Vector3 position, Vector2 size, Vector3 rotationAxis, float rotationAngle)
{
    if (sprite == NULL) return;

    r3d_drawcall_t drawCall = { 0 };

    /* --- Calculation of the transformation matrix --- */

    Matrix matScale = MatrixScale(fabsf(size.x) * 0.5f, -fabsf(size.y) * 0.5f, 1.0f);
    Matrix matRotation = MatrixRotate(rotationAxis, rotationAngle * DEG2RAD);
    Matrix matTranslation = MatrixTranslate(position.x, position.y, position.z);
    Matrix matTransform = MatrixMultiply(MatrixMultiply(matScale, matRotation), matTranslation);

    /* --- Applying transformation to billboard --- */

    switch (sprite->material.billboardMode) {
    case R3D_BILLBOARD_FRONT:
        r3d_transform_to_billboard_front(&matTransform, &R3D.state.transform.invView);
        break;
    case R3D_BILLBOARD_Y_AXIS:
        r3d_transform_to_billboard_y(&matTransform, &R3D.state.transform.invView);
        break;
    default:
        break;
    }

    /* --- Calculation of the representation of the quad in space --- */

    Vector3 axisX = { matTransform.m0 * 0.5f, matTransform.m1 * 0.5f, matTransform.m2 * 0.5f };
    Vector3 axisY = { matTransform.m4 * 0.5f, matTransform.m5 * 0.5f, matTransform.m6 * 0.5f };
    Vector3 center = { matTransform.m12, matTransform.m13, matTransform.m14 };

    drawCall.geometry.sprite.quad[0] = (Vector3) { center.x - axisX.x - axisY.x, center.y - axisX.y - axisY.y, center.z - axisX.z - axisY.z };
    drawCall.geometry.sprite.quad[1] = (Vector3) { center.x + axisX.x - axisY.x, center.y + axisX.y - axisY.y, center.z + axisX.z - axisY.z };
    drawCall.geometry.sprite.quad[2] = (Vector3) { center.x + axisX.x + axisY.x, center.y + axisX.y + axisY.y, center.z + axisX.z + axisY.z };
    drawCall.geometry.sprite.quad[3] = (Vector3) { center.x - axisX.x + axisY.x, center.y - axisX.y + axisY.y, center.z - axisX.z + axisY.z };

    /* --- Finalizing the draw call data --- */

    drawCall.transform = matTransform;
    drawCall.material = sprite->material;
    drawCall.geometryType = R3D_DRAWCALL_GEOMETRY_SPRITE;
    drawCall.renderMode = R3D_DRAWCALL_RENDER_DEFERRED;

    r3d_sprite_get_uv_scale_offset(
        &drawCall.geometry.sprite.uvScale, &drawCall.geometry.sprite.uvOffset, sprite,
        (size.x > 0) ? 1.0f : -1.0f, (size.y > 0) ? 1.0f : -1.0f
    );

    /* --- Added draw call to the right cache depending on render mode --- */

    r3d_array_t* arr = &R3D.container.aDrawDeferred;
    if (sprite->material.blendMode != R3D_BLEND_OPAQUE || R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
        drawCall.renderMode = R3D_DRAWCALL_RENDER_FORWARD;
        arr = &R3D.container.aDrawForward;
    }

    r3d_array_push_back(arr, &drawCall);
}

void R3D_DrawSpriteInstanced(const R3D_Sprite* sprite, const Matrix* instanceTransforms, int instanceCount)
{
    R3D_DrawSpriteInstancedPro(sprite, NULL, MatrixIdentity(), instanceTransforms, 0, NULL, 0, instanceCount);
}

void R3D_DrawSpriteInstancedEx(const R3D_Sprite* sprite, const Matrix* instanceTransforms, const Color* instanceColors, int instanceCount)
{
    R3D_DrawSpriteInstancedPro(sprite, NULL, MatrixIdentity(), instanceTransforms, 0, instanceColors, 0, instanceCount);
}

void R3D_DrawSpriteInstancedPro(const R3D_Sprite* sprite, const BoundingBox* globalAabb, Matrix globalTransform,
                                const Matrix* instanceTransforms, int transformsStride,
                                const Color* instanceColors, int colorsStride,
                                int instanceCount)
{
    r3d_drawcall_t drawCall = { 0 };

    if (sprite == NULL || instanceCount == 0 || instanceTransforms == NULL) {
        return;
    }

    drawCall.transform = globalTransform;
    drawCall.material = sprite->material;
    drawCall.geometryType = R3D_DRAWCALL_GEOMETRY_SPRITE;
    drawCall.renderMode = R3D_DRAWCALL_RENDER_DEFERRED;

    r3d_sprite_get_uv_scale_offset(
        &drawCall.geometry.sprite.uvScale,
        &drawCall.geometry.sprite.uvOffset,
        sprite, 1.0f, -1.0f
    );

    drawCall.instanced.allAabb = globalAabb ? *globalAabb
        : (BoundingBox) {
            { -FLT_MAX, -FLT_MAX, -FLT_MAX },
            { +FLT_MAX, +FLT_MAX, +FLT_MAX }
        };

    drawCall.instanced.transforms = instanceTransforms;
    drawCall.instanced.transStride = transformsStride;
    drawCall.instanced.colStride = colorsStride;
    drawCall.instanced.colors = instanceColors;
    drawCall.instanced.count = instanceCount;

    r3d_array_t* arr = &R3D.container.aDrawDeferredInst;

    if (sprite->material.blendMode != R3D_BLEND_OPAQUE || R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
        drawCall.renderMode = R3D_DRAWCALL_RENDER_FORWARD;
        arr = &R3D.container.aDrawForwardInst;
    }

    r3d_array_push_back(arr, &drawCall);
}

void R3D_DrawParticleSystem(const R3D_ParticleSystem* system, const R3D_Mesh* mesh, const R3D_Material* material)
{
    R3D_DrawParticleSystemEx(system, mesh, material, MatrixIdentity());
}

void R3D_DrawParticleSystemEx(const R3D_ParticleSystem* system, const R3D_Mesh* mesh, const R3D_Material* material, Matrix transform)
{
    if (system == NULL || mesh == NULL) {
        return;
    }

    R3D_DrawMeshInstancedPro(
        mesh, material, &system->aabb, transform,
        &system->particles->transform, sizeof(R3D_Particle),
        &system->particles->color, sizeof(R3D_Particle),
        system->count
    );
}


/* === Internal functions === */

static bool r3d_has_deferred_calls(void)
{
    return (R3D.container.aDrawDeferred.count > 0 || R3D.container.aDrawDeferredInst.count > 0);
}

static bool r3d_has_forward_calls(void)
{
    return (R3D.container.aDrawForward.count > 0 || R3D.container.aDrawForwardInst.count > 0);
}

void r3d_sprite_get_uv_scale_offset(Vector2* uvScale, Vector2* uvOffset, const R3D_Sprite* sprite, float sgnX, float sgnY)
{
    uvScale->x = sgnX / sprite->xFrameCount;
    uvScale->y = sgnY / sprite->yFrameCount;

    int frameIndex = (int)sprite->currentFrame % (sprite->xFrameCount * sprite->yFrameCount);
    int frameX = frameIndex % sprite->xFrameCount;
    int frameY = frameIndex / sprite->xFrameCount;

    uvOffset->x = frameX * uvScale->x;
    uvOffset->y = frameY * uvScale->y;
}

void r3d_depth_stencil_attach(void)
{
    // Attach depth-stencil texture to the framebuffer
    GLuint stencil = R3D.framebuffer.gBuffer.depth;
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, stencil, 0);
}

void r3d_stencil_enable_geometry_write(void)
{
    // Enable stencil test and write the geometry bit
    glEnable(GL_STENCIL_TEST);
    glStencilMask(R3D_STENCIL_GEOMETRY_MASK); // Only write geometry bit
    glStencilFunc(GL_ALWAYS, R3D_STENCIL_GEOMETRY_BIT, R3D_STENCIL_GEOMETRY_MASK);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); // Replace geometry bit on pass
}

void r3d_stencil_enable_geometry_test(GLenum condition)
{
    // Enable stencil test to check geometry bit against a condition
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00); // Disable writing
    glStencilFunc(condition, R3D_STENCIL_GEOMETRY_BIT, R3D_STENCIL_GEOMETRY_MASK);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); // No changes on test
}

void r3d_stencil_enable_effect_write(uint8_t effectID)
{
    // Enable stencil test and write effect ID bits
    glEnable(GL_STENCIL_TEST);
    glStencilMask(R3D_STENCIL_EFFECT_MASK); // Only write effect bits
    glStencilFunc(GL_ALWAYS, effectID & R3D_STENCIL_EFFECT_MASK, R3D_STENCIL_EFFECT_MASK);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); // Replace effect bits on pass
}

void r3d_stencil_enable_effect_test(GLenum condition, uint8_t effectID)
{
    // Enable stencil test to check effect bits against a condition
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0x00); // Disable writing
    glStencilFunc(condition, effectID & R3D_STENCIL_EFFECT_MASK, R3D_STENCIL_EFFECT_MASK);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); // No changes on test
}

void r3d_stencil_enable_effect_write_with_geometry_test(GLenum condition, uint8_t effectID)
{
    glEnable(GL_STENCIL_TEST);

    // Enable effect ID write only if geometry bit passes the test
    glStencilMask(R3D_STENCIL_EFFECT_MASK);
    glStencilFunc(condition, R3D_STENCIL_GEOMETRY_BIT, R3D_STENCIL_GEOMETRY_MASK);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

    // Note: stencil reference value should include desired effectID bits
    // OpenGL will apply the write mask when replacing stencil value
}

void r3d_stencil_disable(void)
{
    glDisable(GL_STENCIL_TEST);
}

void r3d_prepare_process_lights_and_batch(void)
{
    // Clear the previous light batch
    r3d_array_clear(&R3D.container.aLightBatch);

    for (int id = 1; id <= (int)r3d_registry_get_allocated_count(&R3D.container.rLights); id++)
    {
        /* --- Check if the light in the registry is still valid --- */

        if (!r3d_registry_is_valid(&R3D.container.rLights, id)) {
            continue;
        }

        /* --- Get the valid light and check if it is active --- */

        r3d_light_t* light = r3d_registry_get(&R3D.container.rLights, id);
        if (!light->enabled) continue;

        /* --- Process shadow update mode --- */

        if (light->shadow.enabled) {
            r3d_light_process_shadow_update(light);
        }

        /* --- Frustum culling of lights areas --- */

        BoundingBox aabb = r3d_light_get_bounding_box(light);

        if (light->type != R3D_LIGHT_DIR) {
            if (!r3d_frustum_is_aabb_in(&R3D.state.frustum.shape, &aabb)) {
                continue;
            }
        }

        /* --- Here the light is supposed to be visible --- */

        r3d_light_batched_t batched = { .data = light, .aabb = aabb };
        r3d_array_push_back(&R3D.container.aLightBatch, &batched);
    }
}

void r3d_prepare_cull_drawcalls(void)
{
    r3d_drawcall_t* calls = NULL;
    int count = 0;

    /* --- Frustum culling of deferred objects --- */

    calls = (r3d_drawcall_t*)R3D.container.aDrawDeferred.data;
    count = (int)R3D.container.aDrawDeferred.count;
    
    for (int i = count - 1; i >= 0; i--) {
        if (!r3d_drawcall_geometry_is_visible(&calls[i])) {
            calls[i] = calls[--count];
        }
    }

    R3D.container.aDrawDeferred.count = count;

    /* --- Frustum culling of forward objects --- */

    calls = (r3d_drawcall_t*)R3D.container.aDrawForward.data;
    count = (int)R3D.container.aDrawForward.count;
    
    for (int i = count - 1; i >= 0; i--) {
        if (!r3d_drawcall_geometry_is_visible(&calls[i])) {
            calls[i] = calls[--count];
        }
    }

    R3D.container.aDrawForward.count = count;

    /* --- Frustum culling of deferred instanced objects --- */

    calls = (r3d_drawcall_t*)R3D.container.aDrawDeferredInst.data;
    count = (int)R3D.container.aDrawDeferredInst.count;
    
    for (int i = count - 1; i >= 0; i--) {
        if (!r3d_drawcall_instanced_geometry_is_visible(&calls[i])) {
            calls[i] = calls[--count];
        }
    }

    R3D.container.aDrawDeferredInst.count = count;

    /* --- Frustum culling of forward instanced objects --- */

    calls = (r3d_drawcall_t*)R3D.container.aDrawForwardInst.data;
    count = (int)R3D.container.aDrawForwardInst.count;
    
    for (int i = count - 1; i >= 0; i--) {
        if (!r3d_drawcall_instanced_geometry_is_visible(&calls[i])) {
            calls[i] = calls[--count];
        }
    }

    R3D.container.aDrawForwardInst.count = count;
}

void r3d_prepare_sort_drawcalls(void)
{
    if (R3D.state.flags & R3D_FLAG_FORCE_FORWARD) {
        // Here all transparent or opaque objects are contained in the forward array
        if (R3D.state.flags & R3D_FLAG_TRANSPARENT_SORTING) {
            r3d_drawcall_sort_mixed_forward(
                (r3d_drawcall_t*)R3D.container.aDrawForward.data,
                R3D.container.aDrawForward.count
            );
        }
        return;
    }

    // Sort front-to-back for deferred rendering
    if (R3D.state.flags & R3D_FLAG_OPAQUE_SORTING) {
        r3d_drawcall_sort_front_to_back(
            (r3d_drawcall_t*)R3D.container.aDrawDeferred.data,
            R3D.container.aDrawDeferred.count
        );
    }

    // Sort back-to-front for forward rendering
    if (R3D.state.flags & R3D_FLAG_TRANSPARENT_SORTING) {
        r3d_drawcall_sort_back_to_front(
            (r3d_drawcall_t*)R3D.container.aDrawForward.data,
            R3D.container.aDrawForward.count
        );
    }
}

void r3d_prepare_anim_drawcalls(void)
{
    const r3d_array_t* arrays[2] = {
        &R3D.container.aDrawDeferred,
        &R3D.container.aDrawForward,
    };

    for (int i = 0; i < sizeof(arrays) / sizeof(*arrays); i++)
    {
        const r3d_drawcall_t* calls = arrays[i]->data;
        int count = (int)arrays[i]->count;

        for (int j = 0; j < count; j++)
        {
            const r3d_drawcall_t* call = &calls[j];

            if (call->geometryType != R3D_DRAWCALL_GEOMETRY_MODEL || call->geometry.model.anim == NULL) {
                continue;
            }

            if (call->geometry.model.mesh->boneMatrices == NULL) {
                // Only meshes belonging to a model with bones have a boneMatrices cache
                TraceLog(LOG_WARNING, "Attempting to play animation on mesh without bone matrix cache");
            }

            r3d_drawcall_update_model_animation(call);
        }
    }
}

void r3d_pass_shadow_maps(void)
{
    // Config context state
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Push new projection matrix
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();

    // Iterate through all lights to render all geometries
    for (int i = 0; i < R3D.container.aLightBatch.count; i++) {
        r3d_light_batched_t* light = r3d_array_at(&R3D.container.aLightBatch, i);

        // Skip light if it doesn't produce shadows
        if (!light->data->shadow.enabled) continue;

        // Skip if it's not time to update shadows
        if (!light->data->shadow.updateConf.shoudlUpdate) continue;
        else r3d_light_indicate_shadow_update(light->data);

        // TODO: The lights could be sorted to avoid too frequent
        //       state changes, just like with shaders.

        // TODO: The draw calls could also be sorted
        //       according to the shadow cast mode.

        // Start rendering to shadow map
        glBindFramebuffer(GL_FRAMEBUFFER, light->data->shadow.map.id);
        {
            glViewport(0, 0, light->data->shadow.map.resolution, light->data->shadow.map.resolution);

            if (light->data->type == R3D_LIGHT_OMNI) {
                // Set up projection matrix for omni-directional light
                rlMatrixMode(RL_PROJECTION);
                rlSetMatrixProjection(r3d_light_get_matrix_proj_omni(light->data));

                // Render geometries for each face of the cubemap
                for (int j = 0; j < 6; j++) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + j, light->data->shadow.map.depth, 0);
                    glClear(GL_DEPTH_BUFFER_BIT);

                    // Set view matrix for the current cubemap face
                    rlMatrixMode(RL_MODELVIEW);
                    rlLoadIdentity();
                    rlMultMatrixf(MatrixToFloat(r3d_light_get_matrix_view_omni(light->data, j)));

                    // Rasterize geometries for depth rendering
                    r3d_shader_enable(raster.depthCubeInst);
                    {
                        r3d_shader_set_vec3(raster.depthCubeInst, uViewPosition, light->data->position);
                        r3d_shader_set_float(raster.depthCubeInst, uFar, light->data->far);

                        for (size_t k = 0; k < R3D.container.aDrawDeferredInst.count; k++) {
                            r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawDeferredInst.data + k;
                            if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                                r3d_shader_set_float(raster.depthCubeInst, uAlphaCutoff, call->material.alphaCutoff);
                                r3d_drawcall_raster_depth_cube_inst(call, true);
                            }
                        }

                        for (size_t k = 0; k < R3D.container.aDrawForwardInst.count; k++) {
                            r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawForwardInst.data + k;
                            if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                                r3d_shader_set_float(raster.depthCubeInst, uAlphaCutoff, call->material.alphaCutoff);
                                r3d_drawcall_raster_depth_cube_inst(call, true);
                            }
                        }
                    }
                    r3d_shader_enable(raster.depthCube);
                    {
                        r3d_shader_set_vec3(raster.depthCube, uViewPosition, light->data->position);
                        r3d_shader_set_float(raster.depthCube, uFar, light->data->far);

                        for (size_t k = 0; k < R3D.container.aDrawDeferred.count; k++) {
                            r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawDeferred.data + k;
                            if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                                r3d_shader_set_float(raster.depthCube, uAlphaCutoff, call->material.alphaCutoff);
                                r3d_drawcall_raster_depth_cube(call, true);
                            }
                        }

                        for (size_t k = 0; k < R3D.container.aDrawForward.count; k++) {
                            r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawForward.data + k;
                            if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                                r3d_shader_set_float(raster.depthCube, uAlphaCutoff, call->material.alphaCutoff);
                                r3d_drawcall_raster_depth_cube(call, true);
                            }
                        }
                    }
                }
            }
            else {
                // Clear depth buffer for other light types
                glClear(GL_DEPTH_BUFFER_BIT);

                Matrix matView = { 0 };
                Matrix matProj = { 0 };

                if (light->data->type == R3D_LIGHT_DIR) {
                    r3d_light_get_matrix_vp_dir(light->data, R3D.state.scene.bounds, &matView, &matProj);
                }
                else if (light->data->type == R3D_LIGHT_SPOT) {
                    matView = r3d_light_get_matrix_view_spot(light->data);
                    matProj = r3d_light_get_matrix_proj_spot(light->data);
                }

                // Store combined view and projection matrix for the shadow map
                light->data->shadow.matVP = MatrixMultiply(matView, matProj);

                // Set up projection matrix
                rlMatrixMode(RL_PROJECTION);
                rlSetMatrixProjection(matProj);

                // Set up view matrix
                rlMatrixMode(RL_MODELVIEW);
                rlLoadIdentity();
                rlMultMatrixf(MatrixToFloat(matView));

                // Rasterize geometry for depth rendering
                r3d_shader_enable(raster.depthInst);
                {
                    for (size_t j = 0; j < R3D.container.aDrawDeferredInst.count; j++) {
                        r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawDeferredInst.data + j;
                        if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                            r3d_shader_set_float(raster.depthInst, uAlphaCutoff, call->material.alphaCutoff);
                            r3d_drawcall_raster_depth_inst(call, true);
                        }
                    }
                    for (size_t j = 0; j < R3D.container.aDrawForwardInst.count; j++) {
                        r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawForwardInst.data + j;
                        if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                            r3d_shader_set_float(raster.depthInst, uAlphaCutoff, call->material.alphaCutoff);
                            r3d_drawcall_raster_depth_inst(call, true);
                        }
                    }
                }
                r3d_shader_enable(raster.depth);
                {
                    for (size_t j = 0; j < R3D.container.aDrawDeferred.count; j++) {
                        r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawDeferred.data + j;
                        if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                            r3d_shader_set_float(raster.depth, uAlphaCutoff, call->material.alphaCutoff);
                            r3d_drawcall_raster_depth(call, true);
                        }
                    }
                    for (size_t j = 0; j < R3D.container.aDrawForward.count; j++) {
                        r3d_drawcall_t* call = (r3d_drawcall_t*)R3D.container.aDrawForward.data + j;
                        if (call->material.shadowCastMode != R3D_SHADOW_CAST_DISABLED) {
                            r3d_shader_set_float(raster.depth, uAlphaCutoff, call->material.alphaCutoff);
                            r3d_drawcall_raster_depth(call, true);
                        }
                    }
                }
            }
            r3d_shader_disable();
        }
    }
    rlDisableFramebuffer();

    // Reset face to cull
    rlSetCullFace(RL_CULL_FACE_BACK);

    // Pop projection matrix
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();

    // Reset model-view matrix
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

void r3d_clear_gbuffer(bool bindFramebuffer, bool clearColor, bool clearDepth, bool clearStencil)
{
    if (bindFramebuffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.gBuffer.id);
    }

    GLuint bitfield = 0;

    if (clearColor) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        bitfield |= GL_COLOR_BUFFER_BIT;
    }

    if (clearDepth || clearStencil) {
        r3d_depth_stencil_attach();
    }

    if (clearDepth) {
        glClearDepth(1.0f);
        glDepthMask(GL_TRUE);
        bitfield |= GL_DEPTH_BUFFER_BIT;
    }

    if (clearStencil) {
        glClearStencil(0x00);
        glStencilMask(0xFF);
        bitfield |= GL_STENCIL_BUFFER_BIT;
    }

    glClear(bitfield);
}

void r3d_pass_gbuffer(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.gBuffer.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

        /* --- Clear the gbuffer --- */

        r3d_clear_gbuffer(false, true, true, true);

        /* --- Enbale geometry stencil write --- */

        r3d_stencil_enable_geometry_write();

        /* --- Setup RLGL matrices --- */

        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlSetMatrixProjection(R3D.state.transform.proj);

        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(R3D.state.transform.view));

        /* --- Draw geometry with the stencil buffer activated --- */

        r3d_shader_enable(raster.geometryInst);
        {
            for (size_t i = 0; i < R3D.container.aDrawDeferredInst.count; i++) {
                r3d_drawcall_raster_geometry_inst((r3d_drawcall_t*)R3D.container.aDrawDeferredInst.data + i);
            }
        }
        r3d_shader_enable(raster.geometry);
        {
            for (size_t i = 0; i < R3D.container.aDrawDeferred.count; i++) {
                r3d_drawcall_raster_geometry((r3d_drawcall_t*)R3D.container.aDrawDeferred.data + i);
            }
        }
        r3d_shader_disable();

        /* --- Reset RLGL matrices --- */

        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();

        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
    }
}

void r3d_pass_ssao(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPongSSAO.id);
    {
        glViewport(0, 0, R3D.state.resolution.width / 2, R3D.state.resolution.height / 2);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        // Enable gbuffer stencil test (render on geometry)
        if (R3D.state.flags & R3D_FLAG_STENCIL_TEST) {
            r3d_depth_stencil_attach();
            r3d_stencil_enable_geometry_test(GL_EQUAL);
        }
        else {
            r3d_stencil_disable();
        }

        // Bind first SSAO output texture
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            R3D.framebuffer.pingPongSSAO.target, 0
        );

        // Render SSAO
        r3d_shader_enable(screen.ssao);
        {
            r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPongSSAO);

            r3d_shader_set_mat4(screen.ssao, uMatInvProj, R3D.state.transform.invProj);
            r3d_shader_set_mat4(screen.ssao, uMatInvView, R3D.state.transform.invView);
            r3d_shader_set_mat4(screen.ssao, uMatProj, R3D.state.transform.proj);
            r3d_shader_set_mat4(screen.ssao, uMatView, R3D.state.transform.view);

            r3d_shader_set_vec2(screen.ssao, uResolution, (Vector2) {
                (float)R3D.state.resolution.width / 2,
                (float)R3D.state.resolution.height / 2
            });

            r3d_shader_set_float(screen.ssao, uNear, (float)rlGetCullDistanceNear());
            r3d_shader_set_float(screen.ssao, uFar, (float)rlGetCullDistanceFar());

            r3d_shader_set_float(screen.ssao, uRadius, R3D.env.ssaoRadius);
            r3d_shader_set_float(screen.ssao, uBias, R3D.env.ssaoBias);

            r3d_shader_bind_sampler2D(screen.ssao, uTexDepth, R3D.framebuffer.gBuffer.depth);
            r3d_shader_bind_sampler2D(screen.ssao, uTexNormal, R3D.framebuffer.gBuffer.normal);
            r3d_shader_bind_sampler1D(screen.ssao, uTexKernel, R3D.texture.ssaoKernel);
            r3d_shader_bind_sampler2D(screen.ssao, uTexNoise, R3D.texture.ssaoNoise);

            r3d_primitive_bind_and_draw_screen();

            r3d_shader_unbind_sampler2D(screen.ssao, uTexDepth);
            r3d_shader_unbind_sampler2D(screen.ssao, uTexNormal);
            r3d_shader_unbind_sampler1D(screen.ssao, uTexKernel);
            r3d_shader_unbind_sampler2D(screen.ssao, uTexNoise);
        }
        r3d_shader_disable();

        // Blur SSAO
        r3d_shader_enable(generate.gaussianBlurDualPass);
        {
            for (int i = 0, horizontal = true; i < R3D.env.ssaoIterations; i++, horizontal = !horizontal) {
                r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPongSSAO);
                r3d_shader_set_vec2(generate.gaussianBlurDualPass, uTexelDir,
                    (horizontal)
                        ? (Vector2) { R3D.state.resolution.texelX, 0 }
                        : (Vector2) { 0, R3D.state.resolution.texelY }
                );
                r3d_shader_bind_sampler2D(
                    generate.gaussianBlurDualPass, uTexture,
                    R3D.framebuffer.pingPongSSAO.source
                );
                r3d_primitive_bind_and_draw_screen();
            }
        }
        r3d_shader_disable();
    }
}

void r3d_pass_deferred_ambient(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.deferred.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        // Clear color targets only (diffuse/specular)
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Enable gbuffer stencil test (render on geometry)
        if (R3D.state.flags & R3D_FLAG_STENCIL_TEST) {
            r3d_depth_stencil_attach();
            r3d_stencil_enable_geometry_test(GL_EQUAL);
        }
        else {
            r3d_stencil_disable();
        }

        if (R3D.env.useSky)
        {
            // Compute skybox IBL
            r3d_shader_enable(screen.ambientIbl);
            {
                r3d_shader_bind_sampler2D(screen.ambientIbl, uTexAlbedo, R3D.framebuffer.gBuffer.albedo);
                r3d_shader_bind_sampler2D(screen.ambientIbl, uTexNormal, R3D.framebuffer.gBuffer.normal);
                r3d_shader_bind_sampler2D(screen.ambientIbl, uTexDepth, R3D.framebuffer.gBuffer.depth);
                r3d_shader_bind_sampler2D(screen.ambientIbl, uTexORM, R3D.framebuffer.gBuffer.orm);

                if (R3D.env.ssaoEnabled) {
                    r3d_shader_bind_sampler2D(
                        screen.ambientIbl, uTexSSAO,
                        R3D.framebuffer.pingPongSSAO.target
                    );
                }
                else {
                    r3d_shader_bind_sampler2D(
                        screen.ambientIbl, uTexSSAO,
                        R3D.texture.white
                    );
                }

                r3d_shader_bind_samplerCube(screen.ambientIbl, uCubeIrradiance, R3D.env.sky.irradiance.id);
                r3d_shader_bind_samplerCube(screen.ambientIbl, uCubePrefilter, R3D.env.sky.prefilter.id);
                r3d_shader_bind_sampler2D(screen.ambientIbl, uTexBrdfLut, R3D.texture.iblBrdfLut);

                r3d_shader_set_vec3(screen.ambientIbl, uViewPosition, R3D.state.transform.viewPos);
                r3d_shader_set_mat4(screen.ambientIbl, uMatInvProj, R3D.state.transform.invProj);
                r3d_shader_set_mat4(screen.ambientIbl, uMatInvView, R3D.state.transform.invView);
                r3d_shader_set_vec4(screen.ambientIbl, uQuatSkybox, R3D.env.quatSky);

                r3d_primitive_bind_and_draw_screen();

                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexAlbedo);
                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexNormal);
                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexDepth);
                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexSSAO);
                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexORM);

                r3d_shader_unbind_samplerCube(screen.ambientIbl, uCubeIrradiance);
                r3d_shader_unbind_samplerCube(screen.ambientIbl, uCubePrefilter);
                r3d_shader_unbind_sampler2D(screen.ambientIbl, uTexBrdfLut);
            }
            r3d_shader_disable();
        }
        // If no skybox is set, we simply render ambient tint on the meshes.
        else
        {
            // Here we only enable the first attachment (diffuse)
            // and disable the second one, which is the specular,
            // as it should not be written to in this case
            rlActiveDrawBuffers(1);

            r3d_shader_enable(screen.ambient);
            {
                r3d_shader_bind_sampler2D(screen.ambient, uTexAlbedo, R3D.framebuffer.gBuffer.albedo);
                r3d_shader_bind_sampler2D(screen.ambient, uTexORM, R3D.framebuffer.gBuffer.orm);

                if (R3D.env.ssaoEnabled) {
                    r3d_shader_bind_sampler2D(screen.ambient, uTexSSAO, R3D.framebuffer.pingPongSSAO.target);
                }
                else {
                    r3d_shader_bind_sampler2D(screen.ambient, uTexSSAO, R3D.texture.white);
                }

                r3d_shader_set_vec3(screen.ambient, uAmbientColor, R3D.env.ambientColor);

                r3d_primitive_bind_and_draw_screen();

                r3d_shader_unbind_sampler2D(screen.ambient, uTexAlbedo);
                r3d_shader_unbind_sampler2D(screen.ambient, uTexSSAO);
                r3d_shader_unbind_sampler2D(screen.ambient, uTexORM);
            }
            r3d_shader_disable();

            // We now re-enable both attachments,
            // diffuse and specular, for future writes
            rlActiveDrawBuffers(2);
        }
    }
}

void r3d_pass_deferred_lights(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.deferred.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        // Here we disable depth testing and depth writing,
        // and disable face culling for projecting the
        // light volumes into the stencil buffer
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);

        // Setup additive blending to accumulate light contributions
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glBlendEquation(GL_FUNC_ADD);

        // Attaches the stencil buffer for rendering volumes
        r3d_depth_stencil_attach();

        /* --- Bind all textures --- */

        r3d_shader_bind_sampler2D(screen.lighting, uTexAlbedo, R3D.framebuffer.gBuffer.albedo);
        r3d_shader_bind_sampler2D(screen.lighting, uTexNormal, R3D.framebuffer.gBuffer.normal);
        r3d_shader_bind_sampler2D(screen.lighting, uTexDepth, R3D.framebuffer.gBuffer.depth);
        r3d_shader_bind_sampler2D(screen.lighting, uTexORM, R3D.framebuffer.gBuffer.orm);
        r3d_shader_bind_sampler2D(screen.lighting, uTexNoise, R3D.texture.blueNoise);

        /* --- Defines constant uniforms --- */

        r3d_shader_enable(screen.lighting);
        {
            r3d_shader_set_mat4(screen.lighting, uMatInvProj, R3D.state.transform.invProj);
            r3d_shader_set_mat4(screen.lighting, uMatInvView, R3D.state.transform.invView);
            r3d_shader_set_vec3(screen.lighting, uViewPosition, R3D.state.transform.viewPos);
        }

        /* --- Lighting rendering --- */

        for (int i = 0; i < R3D.container.aLightBatch.count; i++)
        {
            r3d_light_batched_t* light = r3d_array_at(&R3D.container.aLightBatch, i);

            // Use an effect ID that avoids 0 (already used by no-effect areas)
            uint8_t lightEffectID = (i + 1) % 127; // Start at 1, wrap to 127
            if (lightEffectID == 0) lightEffectID = 1; // Avoid 0

            // If the light has a volume, we first draw it in the stencil
            // buffer in order to limit the rendering to this area
            if (light->data->type != R3D_LIGHT_DIR) {
                r3d_shader_enable(raster.depthVolume);
                {
                    // TODO: Use the real volumes of lights by projecting cones and spheres

                    Vector3 scale = Vector3Scale(Vector3Subtract(light->aabb.max, light->aabb.min), 0.5f);
                    Vector3 position = Vector3Scale(Vector3Add(light->aabb.min, light->aabb.max), 0.5f);

                    Matrix transform = MatrixScale(scale.x, scale.y, scale.z);
                    transform = MatrixMultiply(transform, MatrixTranslate(position.x, position.y, position.z));
                    r3d_shader_set_mat4(raster.depthVolume, uMatMVP, MatrixMultiply(transform, R3D.state.transform.viewProj));
                    
                    // Stencil setup for volume writing
                    if (R3D.state.flags & R3D_FLAG_STENCIL_TEST) {
                        // Written only in areas where there is geometry
                        r3d_stencil_enable_effect_write_with_geometry_test(GL_EQUAL, lightEffectID);
                    } else {
                        // Written all over the volume of light
                        r3d_stencil_enable_effect_write(lightEffectID);
                    }

                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    r3d_primitive_bind_and_draw_cube();
                }
            }

            // Lighting accumulation pass
            r3d_shader_enable(screen.lighting);
            {
                // If light has volume, render only in areas marked with its effect ID
                if (light->data->type == R3D_LIGHT_DIR) glDisable(GL_STENCIL_TEST);
                else r3d_stencil_enable_effect_test(GL_EQUAL, lightEffectID);

                // Sending data common to each type of light
                r3d_shader_set_vec3(screen.lighting, uLight.color, light->data->color);
                r3d_shader_set_float(screen.lighting, uLight.specular, light->data->specular);
                r3d_shader_set_float(screen.lighting, uLight.energy, light->data->energy);
                r3d_shader_set_int(screen.lighting, uLight.type, light->data->type);

                // Sending specific data according to the type of light
                if (light->data->type == R3D_LIGHT_DIR) {
                    r3d_shader_set_vec3(screen.lighting, uLight.direction, light->data->direction);
                }
                else if (light->data->type == R3D_LIGHT_SPOT) {
                    r3d_shader_set_vec3(screen.lighting, uLight.position, light->data->position);
                    r3d_shader_set_vec3(screen.lighting, uLight.direction, light->data->direction);
                    r3d_shader_set_float(screen.lighting, uLight.range, light->data->range);
                    r3d_shader_set_float(screen.lighting, uLight.attenuation, light->data->attenuation);
                    r3d_shader_set_float(screen.lighting, uLight.innerCutOff, light->data->innerCutOff);
                    r3d_shader_set_float(screen.lighting, uLight.outerCutOff, light->data->outerCutOff);
                }
                else if (light->data->type == R3D_LIGHT_OMNI) {
                    r3d_shader_set_vec3(screen.lighting, uLight.position, light->data->position);
                    r3d_shader_set_float(screen.lighting, uLight.range, light->data->range);
                    r3d_shader_set_float(screen.lighting, uLight.attenuation, light->data->attenuation);
                }

                // Sending shadow map data
                if (light->data->shadow.enabled) {
                    if (light->data->type == R3D_LIGHT_OMNI) {
                        r3d_shader_bind_samplerCube(screen.lighting, uLight.shadowCubemap, light->data->shadow.map.depth);
                    }
                    else {
                        r3d_shader_set_float(screen.lighting, uLight.shadowMapTxlSz, light->data->shadow.map.texelSize);
                        r3d_shader_bind_sampler2D(screen.lighting, uLight.shadowMap, light->data->shadow.map.depth);
                        r3d_shader_set_mat4(screen.lighting, uLight.matVP, light->data->shadow.matVP);
                        if (light->data->type == R3D_LIGHT_DIR) {
                            r3d_shader_set_vec3(screen.lighting, uLight.position, light->data->position);
                        }
                    }
                    r3d_shader_set_float(screen.lighting, uLight.shadowSoftness, light->data->shadow.softness);
                    r3d_shader_set_float(screen.lighting, uLight.shadowBias, light->data->shadow.bias);
                    r3d_shader_set_float(screen.lighting, uLight.near, light->data->near);
                    r3d_shader_set_float(screen.lighting, uLight.far, light->data->far);
                    r3d_shader_set_int(screen.lighting, uLight.shadow, true);
                }
                else {
                    r3d_shader_set_int(screen.lighting, uLight.shadow, false);
                }

                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                r3d_primitive_bind_and_draw_screen();
            }
        }

        /* --- Unbind all textures --- */

        r3d_shader_unbind_sampler2D(screen.lighting, uTexAlbedo);
        r3d_shader_unbind_sampler2D(screen.lighting, uTexNormal);
        r3d_shader_unbind_sampler2D(screen.lighting, uTexDepth);
        r3d_shader_unbind_sampler2D(screen.lighting, uTexORM);
        r3d_shader_unbind_sampler2D(screen.lighting, uTexNoise);

        r3d_shader_unbind_samplerCube(screen.lighting, uLight.shadowCubemap);
        r3d_shader_unbind_sampler2D(screen.lighting, uLight.shadowMap);
    }
}

void r3d_pass_scene_background(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        if (R3D.env.useSky)
        {
            // Setup projection matrix
            rlMatrixMode(RL_PROJECTION);
            rlPushMatrix();
            rlSetMatrixProjection(R3D.state.transform.proj);

            // Setup view matrix
            rlMatrixMode(RL_MODELVIEW);
            rlLoadIdentity();
            rlMultMatrixf(MatrixToFloat(R3D.state.transform.view));

            // Disable backface culling to render the cube from the inside
            // And other pipeline states that are not necessary
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            glDisable(GL_BLEND);

            // Render skybox
            r3d_shader_enable(raster.skybox);
            {
                Matrix matView = rlGetMatrixModelview();
                Matrix matProj = rlGetMatrixProjection();

                r3d_shader_bind_samplerCube(raster.skybox, uCubeSky, R3D.env.sky.cubemap.id);
                r3d_shader_set_vec4(raster.skybox, uRotation, R3D.env.quatSky);
                r3d_shader_set_mat4(raster.skybox, uMatView, matView);
                r3d_shader_set_mat4(raster.skybox, uMatProj, matProj);

                r3d_primitive_bind_and_draw_cube();

                r3d_shader_unbind_samplerCube(raster.skybox, uCubeSky);
            }
            r3d_shader_disable();

            // Reset projection matrix
            rlMatrixMode(RL_PROJECTION);
            rlPopMatrix();

            // Reset view matrix
            rlMatrixMode(RL_MODELVIEW);
            rlLoadIdentity();
        }
        else
        {
            glClearBufferfv(GL_COLOR, 0, (float[4]) {
                R3D.env.backgroundColor.x,
                R3D.env.backgroundColor.y,
                R3D.env.backgroundColor.z,
                0.0f
            });
        }
    }
}

void r3d_pass_scene_deferred(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        // Enable gbuffer stencil test (render on geometry)
        // This is necessary to maintain a correct background
        r3d_depth_stencil_attach();
        r3d_stencil_enable_geometry_test(GL_EQUAL);

        r3d_shader_enable(screen.scene);
        {
            r3d_shader_bind_sampler2D(screen.scene, uTexAlbedo, R3D.framebuffer.gBuffer.albedo);
            r3d_shader_bind_sampler2D(screen.scene, uTexEmission, R3D.framebuffer.gBuffer.emission);
            r3d_shader_bind_sampler2D(screen.scene, uTexDiffuse, R3D.framebuffer.deferred.diffuse);
            r3d_shader_bind_sampler2D(screen.scene, uTexSpecular, R3D.framebuffer.deferred.specular);

            r3d_primitive_bind_and_draw_screen();

            r3d_shader_unbind_sampler2D(screen.scene, uTexAlbedo);
            r3d_shader_unbind_sampler2D(screen.scene, uTexEmission);
            r3d_shader_unbind_sampler2D(screen.scene, uTexDiffuse);
            r3d_shader_unbind_sampler2D(screen.scene, uTexSpecular);
        }
        r3d_shader_disable();

        // Disables the test stencil for subsequent passes
        r3d_stencil_disable();
    }
}

static void r3d_pass_scene_forward_filter_and_send_lights(const r3d_drawcall_t* call)
{
    int lightCount = 0;

    for (int i = 0; lightCount < R3D_SHADER_FORWARD_NUM_LIGHTS && i < R3D.container.aLightBatch.count; i++)
    {
        r3d_light_batched_t* light = r3d_array_at(&R3D.container.aLightBatch, i);

        // Check if the geometry "touches" the light area
        // It's not the most accurate possible but sufficient (?)
        if (light->data->type != R3D_LIGHT_DIR) {
            if (call->geometryType == R3D_DRAWCALL_GEOMETRY_MODEL) {
                if (!CheckCollisionBoxes(light->aabb, call->geometry.model.mesh->aabb)) {
                    continue;
                }
            }
            else if (call->geometryType == R3D_DRAWCALL_GEOMETRY_SPRITE) {
                const Vector3* quad = call->geometry.sprite.quad;
                const BoundingBox* aabb = &light->aabb;
                bool inside = false;
                for (int j = 0; j < 4; ++j) {
                    if (quad[j].x >= aabb->min.x && quad[j].x <= aabb->max.x &&
                        quad[j].y >= aabb->min.y && quad[j].y <= aabb->max.y &&
                        quad[j].z >= aabb->min.z && quad[j].z <= aabb->max.z) {
                        inside = true;
                        break;
                    }
                }
                if (inside) {
                    continue;
                }
            }
        }

        // Use this light, so increment the counter
        lightCount++;

        // Send common data
        r3d_shader_set_int(raster.forward, uLights[i].enabled, true);
        r3d_shader_set_int(raster.forward, uLights[i].type, light->data->type);
        r3d_shader_set_vec3(raster.forward, uLights[i].color, light->data->color);
        r3d_shader_set_float(raster.forward, uLights[i].specular, light->data->specular);
        r3d_shader_set_float(raster.forward, uLights[i].energy, light->data->energy);

        // Send specific data
        if (light->data->type == R3D_LIGHT_DIR) {
            r3d_shader_set_vec3(raster.forward, uLights[i].direction, light->data->direction);
        }
        else if (light->data->type == R3D_LIGHT_SPOT) {
            r3d_shader_set_vec3(raster.forward, uLights[i].position, light->data->position);
            r3d_shader_set_vec3(raster.forward, uLights[i].direction, light->data->direction);
            r3d_shader_set_float(raster.forward, uLights[i].range, light->data->range);
            r3d_shader_set_float(raster.forward, uLights[i].attenuation, light->data->attenuation);
            r3d_shader_set_float(raster.forward, uLights[i].innerCutOff, light->data->innerCutOff);
            r3d_shader_set_float(raster.forward, uLights[i].outerCutOff, light->data->outerCutOff);
        }
        else if (light->data->type == R3D_LIGHT_OMNI) {
            r3d_shader_set_vec3(raster.forward, uLights[i].position, light->data->position);
            r3d_shader_set_float(raster.forward, uLights[i].range, light->data->range);
            r3d_shader_set_float(raster.forward, uLights[i].attenuation, light->data->attenuation);
        }

        // Send shadow map data
        if (light->data->shadow.enabled) {
            if (light->data->type == R3D_LIGHT_OMNI) {
                r3d_shader_bind_samplerCube(raster.forward, uLights[i].shadowCubemap, light->data->shadow.map.depth);
            }
            else {
                r3d_shader_set_float(raster.forward, uLights[i].shadowMapTxlSz, light->data->shadow.map.texelSize);
                r3d_shader_bind_sampler2D(raster.forward, uLights[i].shadowMap, light->data->shadow.map.depth);
                r3d_shader_set_mat4(raster.forward, uMatLightVP[i], light->data->shadow.matVP);
            }
            r3d_shader_set_float(raster.forward, uLights[i].shadowSoftness, light->data->shadow.softness);
            r3d_shader_set_float(raster.forward, uLights[i].shadowBias, light->data->shadow.bias);
            r3d_shader_set_float(raster.forward, uLights[i].near, light->data->near);
            r3d_shader_set_float(raster.forward, uLights[i].far, light->data->far);
            r3d_shader_set_int(raster.forward, uLights[i].shadow, true);
        }
        else {
            r3d_shader_set_int(raster.forward, uLights[i].shadow, false);
        }
    }

    for (int i = lightCount; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
        r3d_shader_set_int(raster.forward, uLights[i].enabled, false);
    }
}

static void r3d_pass_scene_forward_instanced_filter_and_send_lights(const r3d_drawcall_t* call)
{
    int lightCount = 0;

    for (int i = 0; lightCount < R3D_SHADER_FORWARD_NUM_LIGHTS && i < R3D.container.aLightBatch.count; i++)
    {
        r3d_light_batched_t* light = r3d_array_at(&R3D.container.aLightBatch, i);

        // Check if the global instance AABB "touches" the light area
        if (light->data->type != R3D_LIGHT_DIR) {
            if (!CheckCollisionBoxes(light->aabb, call->instanced.allAabb)) {
                continue;
            }
        }

        // Use this light, so increment the counter
        lightCount++;

        // Send common data
        r3d_shader_set_int(raster.forwardInst, uLights[i].enabled, true);
        r3d_shader_set_int(raster.forwardInst, uLights[i].type, light->data->type);
        r3d_shader_set_vec3(raster.forwardInst, uLights[i].color, light->data->color);
        r3d_shader_set_float(raster.forwardInst, uLights[i].specular, light->data->specular);
        r3d_shader_set_float(raster.forwardInst, uLights[i].energy, light->data->energy);

        // Send specific data
        if (light->data->type == R3D_LIGHT_DIR) {
            r3d_shader_set_vec3(raster.forwardInst, uLights[i].direction, light->data->direction);
        }
        else if (light->data->type == R3D_LIGHT_SPOT) {
            r3d_shader_set_vec3(raster.forwardInst, uLights[i].position, light->data->position);
            r3d_shader_set_vec3(raster.forwardInst, uLights[i].direction, light->data->direction);
            r3d_shader_set_float(raster.forwardInst, uLights[i].range, light->data->range);
            r3d_shader_set_float(raster.forwardInst, uLights[i].attenuation, light->data->attenuation);
            r3d_shader_set_float(raster.forwardInst, uLights[i].innerCutOff, light->data->innerCutOff);
            r3d_shader_set_float(raster.forwardInst, uLights[i].outerCutOff, light->data->outerCutOff);
        }
        else if (light->data->type == R3D_LIGHT_OMNI) {
            r3d_shader_set_vec3(raster.forwardInst, uLights[i].position, light->data->position);
            r3d_shader_set_float(raster.forwardInst, uLights[i].range, light->data->range);
            r3d_shader_set_float(raster.forwardInst, uLights[i].attenuation, light->data->attenuation);
        }

        // Send shadow map data
        if (light->data->shadow.enabled) {
            if (light->data->type == R3D_LIGHT_OMNI) {
                r3d_shader_bind_samplerCube(raster.forwardInst, uLights[i].shadowCubemap, light->data->shadow.map.depth);
            }
            else {
                r3d_shader_set_float(raster.forwardInst, uLights[i].shadowMapTxlSz, light->data->shadow.map.texelSize);
                r3d_shader_bind_sampler2D(raster.forwardInst, uLights[i].shadowMap, light->data->shadow.map.depth);
                r3d_shader_set_mat4(raster.forwardInst, uMatLightVP[i], light->data->shadow.matVP);
            }
            r3d_shader_set_float(raster.forwardInst, uLights[i].shadowSoftness, light->data->shadow.softness);
            r3d_shader_set_float(raster.forwardInst, uLights[i].shadowBias, light->data->shadow.bias);
            r3d_shader_set_float(raster.forwardInst, uLights[i].near, light->data->near);
            r3d_shader_set_float(raster.forwardInst, uLights[i].far, light->data->far);
            r3d_shader_set_int(raster.forwardInst, uLights[i].shadow, true);
        }
        else {
            r3d_shader_set_int(raster.forwardInst, uLights[i].shadow, false);
        }
    }

    for (int i = lightCount; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
        r3d_shader_set_int(raster.forwardInst, uLights[i].enabled, false);
    }
}

void r3d_pass_scene_forward_depth_prepass(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        // Setup the depth pre-pass
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        // Reactivation of geometry drawing in the stencil buffer
        r3d_depth_stencil_attach();
        r3d_stencil_enable_geometry_write();

        // Setup projection matrix
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlSetMatrixProjection(R3D.state.transform.proj);

        // Setup view matrix
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(R3D.state.transform.view));

        // Render instanced meshes
        if (R3D.container.aDrawForwardInst.count > 0) {
            r3d_shader_enable(raster.depthInst);
            {
                for (int i = 0; i < R3D.container.aDrawForwardInst.count; i++) {
                    r3d_drawcall_t* call = r3d_array_at(&R3D.container.aDrawForwardInst, i);
                    r3d_drawcall_raster_depth_inst(call, false);
                }
            }
            r3d_shader_disable();
        }

        // Render non-instanced meshes
        if (R3D.container.aDrawForward.count > 0) {
            r3d_shader_enable(raster.depth);
            {
                // We render in reverse order to prioritize drawing the nearest
                // objects first, in order to optimize early depth testing.
                for (int i = (int)R3D.container.aDrawForward.count - 1; i >= 0; i--) {
                    r3d_drawcall_t* call = r3d_array_at(&R3D.container.aDrawForward, i);
                    r3d_drawcall_raster_depth(call, false);
                }
            }
            r3d_shader_disable();
        }

        // Reset projection matrix
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();

        // Reset view matrix
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
    }
}

void r3d_pass_scene_forward(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);

        if (R3D.state.flags & R3D_FLAG_DEPTH_PREPASS) {
            glDepthFunc(GL_EQUAL);
            glDepthMask(GL_FALSE);
        }
        else {
            r3d_depth_stencil_attach();
            r3d_stencil_enable_geometry_write();
            glDepthMask(GL_TRUE);
        }

        // Setup projection matrix
        rlMatrixMode(RL_PROJECTION);
        rlPushMatrix();
        rlSetMatrixProjection(R3D.state.transform.proj);

        // Setup view matrix
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
        rlMultMatrixf(MatrixToFloat(R3D.state.transform.view));

        // Render instanced meshes
        if (R3D.container.aDrawForwardInst.count > 0) {
            r3d_shader_enable(raster.forwardInst);
            {
                r3d_shader_bind_sampler2D(raster.forwardInst, uTexNoise, R3D.texture.blueNoise);

                if (R3D.env.useSky) {
                    r3d_shader_bind_samplerCube(raster.forwardInst, uCubeIrradiance, R3D.env.sky.irradiance.id);
                    r3d_shader_bind_samplerCube(raster.forwardInst, uCubePrefilter, R3D.env.sky.prefilter.id);
                    r3d_shader_bind_sampler2D(raster.forwardInst, uTexBrdfLut, R3D.texture.iblBrdfLut);

                    r3d_shader_set_vec4(raster.forwardInst, uQuatSkybox, R3D.env.quatSky);
                    r3d_shader_set_int(raster.forwardInst, uHasSkybox, true);
                }
                else {
                    r3d_shader_set_vec3(raster.forwardInst, uAmbientColor, R3D.env.ambientColor);
                    r3d_shader_set_int(raster.forwardInst, uHasSkybox, false);
                }

                r3d_shader_set_vec3(raster.forwardInst, uViewPosition, R3D.state.transform.viewPos);

                for (int i = 0; i < R3D.container.aDrawForwardInst.count; i++) {
                    r3d_drawcall_t* call = r3d_array_at(&R3D.container.aDrawForwardInst, i);
                    r3d_pass_scene_forward_instanced_filter_and_send_lights(call);
                    r3d_drawcall_raster_forward_inst(call);
                }

                r3d_shader_unbind_sampler2D(raster.forwardInst, uTexNoise);

                if (R3D.env.useSky) {
                    r3d_shader_unbind_samplerCube(raster.forwardInst, uCubeIrradiance);
                    r3d_shader_unbind_samplerCube(raster.forwardInst, uCubePrefilter);
                    r3d_shader_unbind_sampler2D(raster.forwardInst, uTexBrdfLut);
                }

                for (int i = 0; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
                    r3d_shader_unbind_samplerCube(raster.forwardInst, uLights[i].shadowCubemap);
                    r3d_shader_unbind_sampler2D(raster.forwardInst, uLights[i].shadowMap);
                }
            }
            r3d_shader_disable();
        }

        // Render non-instanced meshes
        if (R3D.container.aDrawForward.count > 0) {
            r3d_shader_enable(raster.forward);
            {
                r3d_shader_bind_sampler2D(raster.forward, uTexNoise, R3D.texture.blueNoise);

                if (R3D.env.useSky) {
                    r3d_shader_bind_samplerCube(raster.forward, uCubeIrradiance, R3D.env.sky.irradiance.id);
                    r3d_shader_bind_samplerCube(raster.forward, uCubePrefilter, R3D.env.sky.prefilter.id);
                    r3d_shader_bind_sampler2D(raster.forward, uTexBrdfLut, R3D.texture.iblBrdfLut);

                    r3d_shader_set_vec4(raster.forward, uQuatSkybox, R3D.env.quatSky);
                    r3d_shader_set_int(raster.forward, uHasSkybox, true);
                }
                else {
                    r3d_shader_set_vec3(raster.forward, uAmbientColor, R3D.env.ambientColor);
                    r3d_shader_set_int(raster.forward, uHasSkybox, false);
                }

                r3d_shader_set_vec3(raster.forward, uViewPosition, R3D.state.transform.viewPos);

                for (int i = 0; i < R3D.container.aDrawForward.count; i++) {
                    r3d_drawcall_t* call = r3d_array_at(&R3D.container.aDrawForward, i);
                    r3d_pass_scene_forward_filter_and_send_lights(call);
                    r3d_drawcall_raster_forward(call);
                }

                r3d_shader_unbind_sampler2D(raster.forward, uTexNoise);

                if (R3D.env.useSky) {
                    r3d_shader_unbind_samplerCube(raster.forward, uCubeIrradiance);
                    r3d_shader_unbind_samplerCube(raster.forward, uCubePrefilter);
                    r3d_shader_unbind_sampler2D(raster.forward, uTexBrdfLut);
                }

                for (int i = 0; i < R3D_SHADER_FORWARD_NUM_LIGHTS; i++) {
                    r3d_shader_unbind_samplerCube(raster.forward, uLights[i].shadowCubemap);
                    r3d_shader_unbind_sampler2D(raster.forward, uLights[i].shadowMap);
                }
            }
            r3d_shader_disable();
        }

        // Reset projection matrix
        rlMatrixMode(RL_PROJECTION);
        rlPopMatrix();

        // Reset view matrix
        rlMatrixMode(RL_MODELVIEW);
        rlLoadIdentity();
    }
}

void r3d_pass_post_setup(void)
{
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glDepthMask(GL_FALSE);
    glStencilMask(0x00);
}

void r3d_pass_post_bloom(void)
{
    /* ---- Generate mip chain --- */

    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.mipChainBloom.id);
    {
        /* --- Bloom: Down Sampling --- */

        r3d_shader_enable(generate.downsampling);
        {
            r3d_shader_set_vec2(generate.downsampling, uTexelSize, (Vector2) {
                (float)R3D.state.resolution.texelX,
                (float)R3D.state.resolution.texelY
            });
            r3d_shader_set_int(generate.downsampling, uMipLevel, 0);

            // Set brightness threshold prefilter data
            r3d_shader_set_vec4(generate.downsampling, uPrefilter, R3D.env.bloomPrefilter);

            // Bind scene color (HDR color buffer) as initial texture input
            r3d_shader_bind_sampler2D(generate.downsampling, uTexture, R3D.framebuffer.pingPong.target);
        
            // Progressively downsample through the mip chain
            for (int i = 0; i < R3D.framebuffer.mipChainBloom.mipCount; i++)
            {
                const struct r3d_mip_bloom* mip = &R3D.framebuffer.mipChainBloom.mipChain[i];

                glViewport(0, 0, mip->w, mip->h);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mip->id, 0);

                // Render screen-filled quad of resolution of current mip
                r3d_primitive_bind_and_draw_screen();

                // Set current mip resolution as srcResolution for next iteration
                r3d_shader_set_vec2(generate.downsampling, uTexelSize, (Vector2) { mip->tx, mip->ty });

                // Set current mip as texture input for next iteration
                glBindTexture(GL_TEXTURE_2D, mip->id);

                // Disable Karis average for consequent downsamples
                r3d_shader_set_int(generate.downsampling, uMipLevel, 1);
            }
        }

        /* --- Bloom: Up Sampling --- */

        r3d_shader_enable(generate.upsampling);
        {
            // Enable additive blending
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            glBlendEquation(GL_FUNC_ADD);

            for (int i = R3D.framebuffer.mipChainBloom.mipCount - 1; i > 0; i--)
            {
                const struct r3d_mip_bloom* mip = &R3D.framebuffer.mipChainBloom.mipChain[i];
                const struct r3d_mip_bloom* nextMip = &R3D.framebuffer.mipChainBloom.mipChain[i-1];

                // Bind viewport and texture from where to read
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mip->id);

                // Set framebuffer render target (we write to this texture)
                glViewport(0, 0, (int)nextMip->w, (int)nextMip->h);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nextMip->id, 0);

                // Set filter radius for the current mip source
                r3d_shader_set_vec2(generate.upsampling, uFilterRadius, (Vector2) {
                    R3D.env.bloomFilterRadius * mip->tx,
                    R3D.env.bloomFilterRadius * mip->ty
                });

                // Render screen-filled quad of resolution of current mip
                r3d_primitive_bind_and_draw_screen();
            }
        
            // Disable additive blending
            glDisable(GL_BLEND);
        }
    }

    /* --- Apply bloom to the scene --- */

    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPong);

        r3d_shader_enable(screen.bloom);
        {
            r3d_shader_bind_sampler2D(screen.bloom, uTexColor, R3D.framebuffer.pingPong.source);
            r3d_shader_bind_sampler2D(screen.bloom, uTexBloomBlur, R3D.framebuffer.mipChainBloom.mipChain[0].id);

            r3d_shader_set_int(screen.bloom, uBloomMode, R3D.env.bloomMode);
            r3d_shader_set_float(screen.bloom, uBloomIntensity, R3D.env.bloomIntensity);

            r3d_primitive_bind_and_draw_screen();
        }
        r3d_shader_disable();
    }
}

void r3d_pass_post_fog(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPong);

        r3d_shader_enable(screen.fog);
        {
            r3d_shader_bind_sampler2D(screen.fog, uTexColor, R3D.framebuffer.pingPong.source);
            r3d_shader_bind_sampler2D(screen.fog, uTexDepth, R3D.framebuffer.gBuffer.depth);

            r3d_shader_set_float(screen.fog, uNear, (float)rlGetCullDistanceNear());
            r3d_shader_set_float(screen.fog, uFar, (float)rlGetCullDistanceFar());
            r3d_shader_set_int(screen.fog, uFogMode, R3D.env.fogMode);
            r3d_shader_set_vec3(screen.fog, uFogColor, R3D.env.fogColor);
            r3d_shader_set_float(screen.fog, uFogStart, R3D.env.fogStart);
            r3d_shader_set_float(screen.fog, uFogEnd, R3D.env.fogEnd);
            r3d_shader_set_float(screen.fog, uFogDensity, R3D.env.fogDensity);

            r3d_primitive_bind_and_draw_screen();
        }
        r3d_shader_disable();
    }
}

void r3d_pass_post_output(void)
{
    R3D_Tonemap tonemap = R3D.env.tonemapMode;

    // Checks if the output shader with the required tonemap exists
    if (R3D.shader.screen.output[tonemap].id == 0) {
        r3d_shader_load_screen_output(tonemap);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPong);

        r3d_shader_enable(screen.output[tonemap]);
        {
            r3d_shader_bind_sampler2D(screen.output[tonemap], uTexColor, R3D.framebuffer.pingPong.source);

            r3d_shader_set_float(screen.output[tonemap], uTonemapExposure, R3D.env.tonemapExposure);
            r3d_shader_set_float(screen.output[tonemap], uTonemapWhite, R3D.env.tonemapWhite);
            r3d_shader_set_float(screen.output[tonemap], uBrightness, R3D.env.brightness);
            r3d_shader_set_float(screen.output[tonemap], uContrast, R3D.env.contrast);
            r3d_shader_set_float(screen.output[tonemap], uSaturation, R3D.env.saturation);
            r3d_shader_set_vec2(screen.output[tonemap], uResolution, (Vector2) {
                (float)R3D.state.resolution.width, (float)R3D.state.resolution.height
            });

            r3d_primitive_bind_and_draw_screen();
        }
        r3d_shader_disable();
    }
}

void r3d_pass_post_fxaa(void)
{
    glBindFramebuffer(GL_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    {
        glViewport(0, 0, R3D.state.resolution.width, R3D.state.resolution.height);

        r3d_framebuffer_swap_pingpong(R3D.framebuffer.pingPong);

        r3d_shader_enable(screen.fxaa);
        {
            r3d_shader_bind_sampler2D(screen.fxaa, uTexture, R3D.framebuffer.pingPong.source);

            r3d_shader_set_vec2(screen.fxaa, uTexelSize, (Vector2) {
                R3D.state.resolution.texelX, R3D.state.resolution.texelY
            });

            r3d_primitive_bind_and_draw_screen();
        }
        r3d_shader_disable();
    }
}

void r3d_pass_final_blit(void)
{
    unsigned int dstId = 0;
    int dstX = 0, dstY = 0;
    int dstW = GetScreenWidth();
    int dstH = GetScreenHeight();

    // If a custom final framebuffer is set, use its ID and dimensions
    if (R3D.framebuffer.customTarget.id != 0) {
        dstId = R3D.framebuffer.customTarget.id;
        dstW = R3D.framebuffer.customTarget.texture.width;
        dstH = R3D.framebuffer.customTarget.texture.height;
    }

    // Maintain aspect ratio if the corresponding flag is set
    if (R3D.state.flags & R3D_FLAG_ASPECT_KEEP) {
        float srcRatio = (float)R3D.state.resolution.width / R3D.state.resolution.height;
        float dstRatio = (float)dstW / dstH;
        if (srcRatio > dstRatio) {
            int prevH = dstH;
            dstH = (int)(dstW * srcRatio + 0.5f);
            dstY = (prevH - dstH) / 2;
        }
        else {
            int prevW = dstW;
            dstW = (int)(dstH * srcRatio + 0.5f);
            dstX = (prevW - dstW) / 2;
        }
    }

    // Bind the destination framebuffer for drawing
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstId);

    // Blit only the color data from the post-processing framebuffer to the main framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, R3D.framebuffer.pingPong.id);
    glBlitFramebuffer(
        0, 0, R3D.state.resolution.width, R3D.state.resolution.height,
        dstX, dstY, dstX + dstW, dstY + dstH, GL_COLOR_BUFFER_BIT,
        (R3D.state.flags & R3D_FLAG_BLIT_LINEAR) ? GL_LINEAR : GL_NEAREST
    );

    // Blit the depth data from the gbuffer framebuffer to the main framebuffer
    glBindFramebuffer(GL_READ_FRAMEBUFFER, R3D.framebuffer.gBuffer.id);
    glBlitFramebuffer(
        0, 0, R3D.state.resolution.width, R3D.state.resolution.height,
        dstX, dstY, dstX + dstW, dstY + dstH,
        GL_DEPTH_BUFFER_BIT, GL_NEAREST
    );
}

void r3d_reset_raylib_state(void)
{
    rlDisableFramebuffer();

    glViewport(0, 0, GetRenderWidth(), GetRenderHeight());

    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    // Here we re-define the blend mode via rlgl to ensure its internal state
    // matches what we've just set manually with OpenGL.

    // It's not enough to change the blend mode only through rlgl, because if we
    // previously used a different blend mode (not "alpha") but rlgl still thinks it's "alpha",
    // then rlgl won't correctly apply the intended blend mode.

    // We do this at the end because calling rlSetBlendMode can trigger a draw call for
    // any content accumulated by rlgl, and we want that to be rendered into the main
    // framebuffer, not into one of R3D's internal framebuffers that will be discarded afterward.

    // TODO: Ideally, we would retrieve rlgl’s current blend mode state and restore it exactly.

    rlSetBlendMode(RL_BLEND_ALPHA);
}
