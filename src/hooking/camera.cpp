#include "pch.h"
#include "cemu_hooks.h"
#include "rendering/openxr.h"
#include "../instance.h"


void CemuHooks::hook_BeginCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    OpenXR::EyeSide side = hCPU->gpr[0] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    Log::print("");
    Log::print("===============================================================================");
    Log::print("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", side == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");

    bool layersInitialized = VRManager::instance().XR->GetRenderer()->m_layer3D && VRManager::instance().XR->GetRenderer()->m_layer2D;
    if (layersInitialized && side == OpenXR::EyeSide::LEFT) {
        if (VRManager::instance().VK->m_imguiOverlay) {
            VRManager::instance().VK->m_imguiOverlay->BeginFrame();
            VRManager::instance().VK->m_imguiOverlay->Update();
        }
        VRManager::instance().XR->GetRenderer()->StartFrame();
    }
}

// TODO FOR NEXT TIME: We change GetRenderCamera() in all cases, but this apparently even causes the camera to be based off of it despite us changing the
// TODO FOR NEXT TIME: Should see if we could disable the double input pulling. We could do it only for the right frame.
// TODO FOR NEXT TIME: I actually wrote down a bunch of addresses for the camera following. At least hooking these to have a camera that is rotated would solve the issue of walking without a head.

// 0x02b8f508 = li r3, 0
// 0x02B8F508 = li r3, 0
// 0x2B9B930 = li r3, 0


//static glm::fquat extractYaw(const glm::fquat& q) {
//    // filter out everything but the yaw, with a world normal of 0 1 0
//    glm::fquat n = glm::normalize(q);
//    float yaw = glm::yaw(n);
//    // 3. Re-create a quaternion that rotates *only* around the global +Y axis. AngleAxis expects (angle, axis). GLM angles are in radians.
//    return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
//}

void CemuHooks::hook_updateCameraOLD(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    // Read the camera matrix from the game's memory
    //uint32_t ppc_cameraMatrixOffsetIn = hCPU->gpr[31];
    //uint32_t ppc_cameraSide = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;
    //ActCamera origCameraMatrix = {};
    //readMemory(ppc_cameraMatrixOffsetIn, &origCameraMatrix);

    ////Log::print("[{}] Updated camera position", ppc_cameraSide == OpenXR::EyeSide::LEFT ? "left" : "right");

    //// Current VR headset camera matrix
    //auto views = VRManager::instance().XR->GetRenderer()->GetMiddlePose();
    //if (!views) {
    //    Log::print("[ERROR] hook_updateCamera: No views available for the middle pose.");
    //    return;
    //}

    //glm::fquat playerYaw = extractYaw(views.value());


    //// Current in-game camera matrix
    //glm::fvec3 oldCameraPosition = glm::fvec3(origCameraMatrix.finalCamMtx.x_x.getLE(), origCameraMatrix.finalCamMtx.y_x.getLE(), origCameraMatrix.finalCamMtx.z_x.getLE());
    //glm::fvec3 oldCameraTarget = glm::fvec3(origCameraMatrix.finalCamMtx.pos_x.getLE(), origCameraMatrix.finalCamMtx.y_x.getLE(), origCameraMatrix.finalCamMtx.y_y.getLE());
    //float oldCameraDistance = glm::distance(oldCameraPosition, oldCameraTarget);

    //// Calculate game view directions
    //glm::fvec3 forwardVector = glm::normalize(oldCameraTarget - oldCameraPosition);
    //glm::fquat lookAtQuat = glm::quatLookAtRH(forwardVector, { 0.0, 1.0, 0.0 });

    //// Calculate new view direction
    //glm::fquat combinedQuat = glm::normalize(lookAtQuat * playerYaw);
    //glm::fmat3 combinedMatrix = glm::mat3_cast(combinedQuat);

    //origCameraMatrix.finalCamMtx.x_x = oldCameraPosition.x;
    //origCameraMatrix.finalCamMtx.y_x = oldCameraPosition.y;
    //origCameraMatrix.finalCamMtx.z_x = oldCameraPosition.z;
    //// pos + rotated headset pos + inverted forward direction after combining both the in-game and HMD rotation
    //origCameraMatrix.finalCamMtx.pos_x = oldCameraPosition.x + ((combinedMatrix[2][0] * -1.0f) * oldCameraDistance);
    //origCameraMatrix.finalCamMtx.x_y = oldCameraPosition.y + ((combinedMatrix[2][1] * -1.0f) * oldCameraDistance);
    //origCameraMatrix.finalCamMtx.z_y = oldCameraPosition.z + ((combinedMatrix[2][2] * -1.0f) * oldCameraDistance);

    //// Write the camera matrix to the game's memory
    //uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[31];
    //writeMemory(ppc_cameraMatrixOffsetOut, &origCameraMatrix);
    s_framesSinceLastCameraUpdate = 0;
}



XrTime s_lastTimestamp = -1;
constexpr float kYawSpeed = glm::radians(60.f);
constexpr float kDeadzone = 0.3f;

inline float SecondsBetween(XrTime a, XrTime b) { return static_cast<float>(a - b) * 1e-9f; }
inline glm::vec2 WithDeadzone(glm::vec2 v) { return (glm::length(v) < kDeadzone) ? glm::vec2(0) : v; }


void CemuHooks::hook_CameraRotationControl(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    // set r3 to 0 so that the normal camera chase isn't ran
    hCPU->gpr[3] = 0;

    auto input = VRManager::instance().XR->m_input.load();
    if (!input.inGame.in_game) return;

    const glm::vec2 stick = WithDeadzone(ToGLM(input.inGame.camera.currentState));
    const XrTime now = input.inGame.inputTime;

    if (s_lastTimestamp == -1) {
        s_lastTimestamp = now;
        return;
    }
    const float dt = SecondsBetween(now, s_lastTimestamp);
    s_lastTimestamp = now;
    if (dt <= 0) return;

    const glm::fquat qYaw = glm::angleAxis(-stick.x * kYawSpeed * dt, glm::vec3(0.f, 1.f, 0.f));

    //VRManager::instance().XR->m_inputCameraRotation = glm::normalize(VRManager::instance().XR->m_inputCameraRotation.load() * qYaw);
}


std::pair<glm::quat, glm::quat> swingTwistY(const glm::quat& q) {
    glm::vec3 yAxis(0, 1, 0);
    // Project rotation axis onto Y to get twist
    glm::vec3 r(q.x, q.y, q.z);
    float dot = glm::dot(r, yAxis);
    glm::vec3 proj = yAxis * dot;
    glm::quat twist = glm::normalize(glm::quat(q.w, proj.x, proj.y, proj.z));
    glm::quat swing = q * glm::conjugate(twist);
    return { swing, twist };
};


glm::fquat CemuHooks::s_cameraRotation = {};
glm::fquat CemuHooks::s_forwardRotation = glm::angleAxis(-90.0f, glm::vec3(0.0f, 1.0f, 0.0f));

void CemuHooks::hook_GetRenderCamera(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    uint32_t cameraIn = hCPU->gpr[3];
    uint32_t cameraOut = hCPU->gpr[12];
    OpenXR::EyeSide cameraSide = hCPU->gpr[11] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    BESeadLookAtCamera camera = {};
    readMemory(cameraIn, &camera);

    Log::print("[PPC] Getting render camera for {} side", cameraSide == OpenXR::EyeSide::LEFT ? "left" : "right");

    // in-game camera
    glm::mat3x4 originalMatrix = camera.mtx.getLEMatrix();
    glm::mat4 viewGame = glm::transpose(originalMatrix);
    glm::mat4 worldGame = glm::inverse(viewGame);
    glm::quat baseRot = glm::quat_cast(worldGame);
    auto [swing, baseYaw] = swingTwistY(baseRot);

    glm::vec3 basePos = glm::vec3(worldGame[3]);


    // take link's direction, then rotate the headset position
    BEMatrix34 mtx = {};
    readMemory(s_playerMtxAddress, &mtx);
    glm::fvec3 playerPos = mtx.getPos().getLE();
    glm::fquat playerRot = mtx.getRotLE();

    // vr camera
    std::optional<XrPosef> currPoseOpt = VRManager::instance().XR->GetRenderer()->GetPose(cameraSide);
    if (!currPoseOpt.has_value())
        return;
    glm::fvec3 eyePos = ToGLM(currPoseOpt.value().position);
    glm::fquat eyeRot = ToGLM(currPoseOpt.value().orientation);

    s_cameraRotation = baseYaw;
    s_forwardRotation = s_cameraRotation * glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    // todo: we moved to our own custom camera system, but we could see if the CameraOld method might've worked if we swapped the order of the quaternion multiplications. Apparently it's important.

    glm::vec3 newPos = playerPos + (baseYaw * eyePos);
    glm::fquat newRot = baseYaw * eyeRot;

    glm::mat4 newWorldVR = glm::translate(glm::mat4(1.0f), newPos) * glm::mat4_cast(newRot);
    glm::mat4 newViewVR = glm::inverse(newWorldVR);
    glm::mat3x4 rowMajor = glm::transpose(newViewVR);

    camera.mtx.setLEMatrix(rowMajor);
    camera.pos = newPos;

    // Set look-at point by offsetting position in view direction
    glm::vec3 viewDir = -glm::vec3(rowMajor[2]); // Forward direction is -Z in view space
    camera.at = newPos + viewDir;

    // Transform world up vector by new rotation
    glm::vec3 upDir = glm::vec3(rowMajor[1]); // Up direction is +Y in view space
    camera.up = upDir;

    glm::fvec3 yawDegrees = glm::eulerAngles(baseRot);
    float yawRotation = yawDegrees.x >= 3.0f ? yawDegrees.y : -yawDegrees.y + glm::radians(180.0f);
    //Log::print("!! In-game camera position = {}, In-game camera rotation = {} degrees", basePos, yawRotation);
    //Log::print("!! Link's position: {}, Link's rotation = {} degrees", playerPos, playerRot);

    writeMemory(cameraOut, &camera);
    hCPU->gpr[3] = cameraOut;
}

constexpr uint32_t seadOrthoProjection = 0x1027B5BC;
constexpr uint32_t seadPerspectiveProjection = 0x1027B54C;


// https://github.com/KhronosGroup/OpenXR-SDK/blob/858912260ca616f4c23f7fb61c89228c353eb124/src/common/xr_linear.h#L564C1-L632C2
// https://github.com/aboood40091/sead/blob/45b629fb032d88b828600a1b787729f2d398f19d/engine/library/modules/src/gfx/seadProjection.cpp#L166

data_VRProjectionMatrixOut calculateFOVAndOffset(XrFovf viewFOV) {
    float totalHorizontalFov = viewFOV.angleRight - viewFOV.angleLeft;
    float totalVerticalFov = viewFOV.angleUp - viewFOV.angleDown;

    float aspectRatio = totalHorizontalFov / totalVerticalFov;
    float fovY = totalVerticalFov;
    float projectionCenter_offsetX = (viewFOV.angleRight + viewFOV.angleLeft) / 2.0f;
    float projectionCenter_offsetY = (viewFOV.angleUp + viewFOV.angleDown) / 2.0f;

    data_VRProjectionMatrixOut ret = {};
    ret.aspectRatio = aspectRatio;
    ret.fovY = fovY;
    ret.offsetX = projectionCenter_offsetX;
    ret.offsetY = projectionCenter_offsetY;

    return ret;
}

static glm::mat4 calculateProjectionMatrix(float nearZ, float farZ, const XrFovf& fov) {
    float l = tanf(fov.angleLeft) * nearZ;
    float r = tanf(fov.angleRight) * nearZ;
    float b = tanf(fov.angleDown) * nearZ;
    float t = tanf(fov.angleUp) * nearZ;

    float invW = 1.0f / (r - l);
    float invH = 1.0f / (t - b);
    float invD = 1.0f / (farZ - nearZ);

    glm::mat4 dst = {};
    dst[0][0] = 2.0f * nearZ * invW;
    dst[1][1] = 2.0f * nearZ * invH;
    dst[0][2] = (r + l) * invW;
    dst[1][2] = (t + b) * invH;
    dst[2][2] = -(farZ + nearZ) * invD;
    dst[2][3] = -(2.0f * farZ * nearZ) * invD;
    dst[3][2] = -1.0f;
    dst[3][3] = 0.0f;

    return dst;
}

void CemuHooks::hook_GetRenderProjection(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t projectionIn = hCPU->gpr[3];
    uint32_t projectionOut = hCPU->gpr[12];
    OpenXR::EyeSide side = hCPU->gpr[0] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    BESeadPerspectiveProjection perspectiveProjection = {};
    readMemory(projectionIn, &perspectiveProjection);

    if (perspectiveProjection.zFar == 10000.0f) {
        return;
    }

    // Log::print("Render Proj. (LR: {:08X}): {}", hCPU->sprNew.LR, perspectiveProjection);
    // Log::print("[PPC] Getting render projection for {} side", side == OpenXR::EyeSide::LEFT ? "left" : "right");

    if (!VRManager::instance().XR->GetRenderer()->GetFOV(side).has_value()) {
        return;
    }
    XrFovf currFOV = VRManager::instance().XR->GetRenderer()->GetFOV(side).value();
    auto newProjection = calculateFOVAndOffset(currFOV);

    perspectiveProjection.aspect = newProjection.aspectRatio;
    perspectiveProjection.fovYRadiansOrAngle = newProjection.fovY;
    float halfAngle = newProjection.fovY.getLE() * 0.5f;
    perspectiveProjection.fovySin = sinf(halfAngle);
    perspectiveProjection.fovyCos = cosf(halfAngle);
    perspectiveProjection.fovyTan = tanf(halfAngle);
    perspectiveProjection.offset.x = newProjection.offsetX;
    perspectiveProjection.offset.y = newProjection.offsetY;

    glm::fmat4 newMatrix = calculateProjectionMatrix(perspectiveProjection.zNear.getLE(), perspectiveProjection.zFar.getLE(), currFOV);
    perspectiveProjection.matrix = newMatrix;

    // calculate device matrix
    glm::fmat4 newDeviceMatrix = newMatrix;

    float zScale = perspectiveProjection.deviceZScale.getLE();
    float zOffset = perspectiveProjection.deviceZOffset.getLE();

    newDeviceMatrix[2][0] *= zScale;
    newDeviceMatrix[2][1] *= zScale;
    newDeviceMatrix[2][2] = (newDeviceMatrix[2][2] + newDeviceMatrix[3][2] * zOffset) * zScale;
    newDeviceMatrix[2][3] = newDeviceMatrix[2][3] * zScale + newDeviceMatrix[3][3] * zOffset;

    perspectiveProjection.deviceMatrix = newDeviceMatrix;

    perspectiveProjection.dirty = false;
    perspectiveProjection.deviceDirty = false;

    writeMemory(projectionOut, &perspectiveProjection);
    hCPU->gpr[3] = projectionOut;
}


void CemuHooks::hook_ModifyLightPrePassProjectionMatrix(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    uint32_t projectionIn = hCPU->gpr[3];
    OpenXR::EyeSide side = hCPU->gpr[11] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    BESeadPerspectiveProjection perspectiveProjection = {};
    readMemory(projectionIn, &perspectiveProjection);

    if (!VRManager::instance().XR->GetRenderer()->GetFOV(side).has_value()) {
        return;
    }

    XrFovf currFOV = VRManager::instance().XR->GetRenderer()->GetFOV(side).value();
    auto newProjection = calculateFOVAndOffset(currFOV);

    perspectiveProjection.aspect = newProjection.aspectRatio;
    perspectiveProjection.fovYRadiansOrAngle = newProjection.fovY;
    float halfAngle = newProjection.fovY.getLE() * 0.5f;
    perspectiveProjection.fovySin = sinf(halfAngle);
    perspectiveProjection.fovyCos = cosf(halfAngle);
    perspectiveProjection.fovyTan = tanf(halfAngle);
    perspectiveProjection.offset.x = newProjection.offsetX;
    perspectiveProjection.offset.y = newProjection.offsetY;

    glm::fmat4 newMatrix = calculateProjectionMatrix(perspectiveProjection.zNear.getLE(), perspectiveProjection.zFar.getLE(), currFOV);
    perspectiveProjection.matrix = newMatrix;

    // calculate device matrix
    glm::fmat4 newDeviceMatrix = newMatrix;

    float zScale = perspectiveProjection.deviceZScale.getLE();
    float zOffset = perspectiveProjection.deviceZOffset.getLE();

    newDeviceMatrix[2][0] *= zScale;
    newDeviceMatrix[2][1] *= zScale;
    newDeviceMatrix[2][2] = (newDeviceMatrix[2][2] + newDeviceMatrix[3][2] * zOffset) * zScale;
    newDeviceMatrix[2][3] = newDeviceMatrix[2][3] * zScale + newDeviceMatrix[3][3] * zOffset;

    perspectiveProjection.deviceMatrix = newDeviceMatrix;

    perspectiveProjection.dirty = false;
    perspectiveProjection.deviceDirty = false;

    writeMemory(projectionIn, &perspectiveProjection);
}


//float previousAddedAngle = 0.0f;
void CemuHooks::hook_ApplyCameraRotation(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    // float degrees = hCPU->fpr[1].fp0;
    // float addedAngle = degrees - previousAddedAngle;
    // previousAddedAngle = degrees;
    //
    // // rotate the camera with whatever the OpenXR rotation is
    // XrPosef currFOV = VRManager::instance().XR->GetRenderer()->GetPose(OpenXR::EyeSide::LEFT).value();
    // glm::fquat currQuat = ToGLM(currFOV.orientation);
    // glm::fquat rotateHorizontalCounter = glm::angleAxis(glm::radians(addedAngle), glm::fvec3(0.0f, 1.0f, 0.0f));
    // currQuat = rotateHorizontalCounter * currQuat;
    //
    // float rotatedDegrees = glm::degrees(glm::atan(currQuat.x, currQuat.w));
    //
    // hCPU->fpr[1].fp0 = rotatedDegrees;
}

void CemuHooks::hook_EndCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    OpenXR::EyeSide side = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    if (VRManager::instance().XR->GetRenderer()->IsInitialized() && side == OpenXR::EyeSide::RIGHT) {
        VRManager::instance().XR->GetRenderer()->EndFrame();
        CemuHooks::m_heldWeaponsLastUpdate[0] = CemuHooks::m_heldWeaponsLastUpdate[0]++;
        CemuHooks::m_heldWeaponsLastUpdate[1] = CemuHooks::m_heldWeaponsLastUpdate[1]++;
        if (CemuHooks::m_heldWeaponsLastUpdate[0] >= 6) {
            CemuHooks::m_heldWeapons[0] = 0;
        }
        if (CemuHooks::m_heldWeaponsLastUpdate[1] >= 6) {
            CemuHooks::m_heldWeapons[1] = 0;
        }
    }

    Log::print("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", side == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");
    Log::print("===============================================================================");
    Log::print("");
}

void CemuHooks::hook_UseCameraDistance(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    if (GetSettings().IsFirstPersonMode()) {
        hCPU->fpr[13].fp0 = 0.0f;
    }
    else {
        hCPU->fpr[13].fp0 = hCPU->fpr[10].fp0;
    }
}

constexpr uint32_t playerVTable = 0x101E5FFC;

void CemuHooks::hook_SetActorOpacity(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    double toBeSetOpacity = hCPU->fpr[1].fp0;
    uint32_t actorPtr = hCPU->gpr[3];

    float actual_transparency = 1.0f;

    ActorWiiU actor;
    readMemory(actorPtr, &actor);

    if (GetSettings().IsFirstPersonMode()) {
        //uint32_t actorVTable = actor.vtable.getLE();
        //if (actorVTable == playerVTable) {
        //    //Log::print("[PPC] Setting player opacity to {} for actor at {:08X}", toBeSetOpacity, actorPtr);
        //    toBeSetOpacity = 1.0;
        //    uint8_t opacityOrDoFlushOpacityToGPU = 1;
        //    //writeMemoryBE(actorPtr + offsetof(ActorWiiU, startModelOpacity), &toBeSetOpacity);
        //    writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
        //    writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);
        //    return;
        //}

        // prevent the game from hiding weapons that clip into the player model or are hidden for some other reason
        if (m_heldWeapons[0] == actorPtr || m_heldWeapons[1] == actorPtr) {
            if (hCPU->sprNew.LR == 0x024A410C) {
                //Log::print("!! PREVENTING setting the weapon opacity to {} for a held weapon {} for actor at {:08X} (LR = {:08X})", toBeSetOpacity, actor.name.getLE(), actorPtr, hCPU->sprNew.LR);
                //toBeSetOpacity = 1.0;
                //uint8_t opacityOrDoFlushOpacityToGPU = 0;
                //writeMemoryBE(actorPtr + offsetof(ActorWiiU, startModelOpacity), &toBeSetOpacity);
                //writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
                //toBeSetOpacity = 1.0;
                //writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacityRelated), &toBeSetOpacity);
                //writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);
                return;
            }

            if (hCPU->sprNew.LR == 0x024A4118) {
                //Log::print("!! PREVENTING setting the weapon opacity to {} for a held weapon {} for actor at {:08X} (LR = {:08X})", toBeSetOpacity, actor.name.getLE(), actorPtr, hCPU->sprNew.LR);
                toBeSetOpacity = 1.0f;
                uint8_t opacityOrDoFlushOpacityToGPU = 0;
                writeMemoryBE(actorPtr + offsetof(ActorWiiU, startModelOpacity), &toBeSetOpacity);
                writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
                toBeSetOpacity = 1.0;
                writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacityRelated), &toBeSetOpacity);
                writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);

                //hCPU->fpr[30].fp0 = 0.0f;
                //hCPU->fpr[1].fp0 = 1.0f;
                hCPU->instructionPointer = 0x024A4128;
            }

            //Log::print("!! PREVENTING setting the weapon opacity to {} for a held weapon {} for actor at {:08X} (LR = {:08X})", toBeSetOpacity, actor.name.getLE(), actorPtr, hCPU->sprNew.LR);
            toBeSetOpacity = 1.0f;
            uint8_t opacityOrDoFlushOpacityToGPU = 1;
            if (hCPU->sprNew.LR == 0x024A6B18) {
                float negativeOpacity = 0.0;
                hCPU->fpr[31].fp0 = 1.0f;
                hCPU->fpr[1].fp0 = 1.0f;
                opacityOrDoFlushOpacityToGPU = 0;
                writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacityRelated), &negativeOpacity);
            }
            writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
            writeMemoryBE(actorPtr + offsetof(ActorWiiU, startModelOpacity), &toBeSetOpacity);
            writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);

            //if (hCPU->sprNew.LR == 0x024A4118) {
            //    hCPU->fpr[30].fp0 = 0.0f;
            //    hCPU->fpr[1].fp0 = 1.0f;
            //    hCPU->instructionPointer = 0x024A4128;
            //}
            return;
        }
    }

    //Log::print("!! Currently held weapon ptrs: {:08X} {:08X}", CemuHooks::m_heldWeapons[0], CemuHooks::m_heldWeapons[1]);
    if (actor.modelOpacity.getLE() != toBeSetOpacity) {
        uint8_t opacityOrDoFlushOpacityToGPU = 1;
        writeMemoryBE(actorPtr + offsetof(ActorWiiU, modelOpacity), &toBeSetOpacity);
        writeMemoryBE(actorPtr + offsetof(ActorWiiU, opacityOrDoFlushOpacityToGPU), &opacityOrDoFlushOpacityToGPU);
    }
}