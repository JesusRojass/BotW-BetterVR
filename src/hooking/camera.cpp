#include "cemu_hooks.h"
#include "instance.h"
#include "rendering/openxr.h"


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

OpenXR::EyeSide s_currentEye = OpenXR::EyeSide::RIGHT;
std::pair<data_VRCameraRotationOut, OpenXR::EyeSide> s_currentCameraRotation = {};

glm::fvec3 g_lookAtPos;
glm::fquat g_lookAtQuat;

// todo: for non-EAR versions it should use the same camera inputs for both eyes
void CemuHooks::hook_UpdateCameraPositionAndTarget(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    Log::print("[{}] Updated camera position", s_currentEye == OpenXR::EyeSide::LEFT ? "left" : "right");

    // Read the camera matrix from the game's memory
    uint32_t ppc_cameraMatrixOffsetIn = hCPU->gpr[7];
    data_VRCameraIn origCameraMatrix = {};

    readMemory(ppc_cameraMatrixOffsetIn, &origCameraMatrix);

    // Current VR headset camera matrix
    XrPosef currPose = VRManager::instance().XR->GetRenderer()->m_layer3D.GetPose(s_currentEye);
    XrFovf currFov = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(s_currentEye);

    glm::fvec3 currEyePos(currPose.position.x, currPose.position.y, currPose.position.z);
    glm::fquat currEyeQuat(currPose.orientation.w, currPose.orientation.x, currPose.orientation.y, currPose.orientation.z);

    // Current in-game camera matrix
    glm::fvec3 oldCameraPosition(origCameraMatrix.posX, origCameraMatrix.posY, origCameraMatrix.posZ);
    // todo: test if third-person mode camera can still be controlled adequately with player movement stick
    glm::fvec3 oldCameraTarget(origCameraMatrix.targetX, CemuHooks::GetSettings().IsFirstPersonMode() || true ? origCameraMatrix.posY : origCameraMatrix.targetY, origCameraMatrix.targetZ);
    float oldCameraDistance = glm::distance(oldCameraPosition, oldCameraTarget);

    // Calculate game view directions
    glm::fvec3 forwardVector = glm::normalize(oldCameraTarget - oldCameraPosition);
    glm::fquat lookAtQuat = glm::quatLookAtRH(forwardVector, { 0.0, 1.0, 0.0 });

    // Calculate new view direction
    glm::fquat combinedQuat = glm::normalize(lookAtQuat * currEyeQuat);
    glm::fmat3 combinedMatrix = glm::toMat3(combinedQuat);

    // Rotate the headset position by the in-game rotation
    glm::fvec3 rotatedHmdPos = lookAtQuat * currEyePos;

    data_VRCameraOut updatedCameraMatrix = {};
    updatedCameraMatrix.enabled = true;
    updatedCameraMatrix.posX = oldCameraPosition.x + rotatedHmdPos.x;
    updatedCameraMatrix.posY = oldCameraPosition.y + rotatedHmdPos.y + CemuHooks::GetSettings().playerHeightSetting.getLE();
    updatedCameraMatrix.posZ = oldCameraPosition.z + rotatedHmdPos.z;
    // pos + rotated headset pos + inverted forward direction after combining both the in-game and HMD rotation
    updatedCameraMatrix.targetX = oldCameraPosition.x + rotatedHmdPos.x + ((combinedMatrix[2][0] * -1.0f) * oldCameraDistance);
    updatedCameraMatrix.targetY = oldCameraPosition.y + rotatedHmdPos.y + ((combinedMatrix[2][1] * -1.0f) * oldCameraDistance) + CemuHooks::GetSettings().playerHeightSetting.getLE();
    updatedCameraMatrix.targetZ = oldCameraPosition.z + rotatedHmdPos.z + ((combinedMatrix[2][2] * -1.0f) * oldCameraDistance);

    // set the lookAt position and quaternion with offset to be able to translate the controller position to the game world
    g_lookAtPos = oldCameraPosition;
    g_lookAtPos.y += CemuHooks::GetSettings().playerHeightSetting.getLE();
    g_lookAtQuat = lookAtQuat;

    // manages pivot, roll and pitch presumably
    s_currentCameraRotation.first.rotY = combinedMatrix[1][1];
    s_currentCameraRotation.first.rotX = combinedMatrix[1][0];
    s_currentCameraRotation.first.rotZ = combinedMatrix[1][2];
    s_currentCameraRotation.second = s_currentEye;

    // Write the camera matrix to the game's memory
    uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[3];
    writeMemory(ppc_cameraMatrixOffsetOut, &updatedCameraMatrix);
    s_framesSinceLastCameraUpdate = 0;
}


void CemuHooks::hook_BeginCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    s_currentEye = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::LEFT : OpenXR::EyeSide::RIGHT;

    Log::print("");
    Log::print("===============================================================================");
    Log::print("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", s_currentEye == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");
}

void CemuHooks::hook_GetRenderCamera(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    uint32_t cameraIn = hCPU->gpr[3];
    uint32_t cameraOut = hCPU->gpr[12];

    BESeadLookAtCamera camera = {};
    readMemory(cameraIn, &camera);

    if (camera.pos.x.getLE() != 0.0f) {
        s_framesSinceLastCameraUpdate = 0;
        // Log::print("[MODIFIED] Getting render camera (with LR: {:08X}): {}", hCPU->sprNew.LR, camera);

        // in-game camera
        glm::mat3x4 originalMatrix = camera.mtx.getLEMatrix();
        glm::mat4 viewGame = glm::transpose(originalMatrix);
        glm::mat4 worldGame = glm::inverse(viewGame);
        glm::quat baseRot  = glm::quat_cast(worldGame);
        glm::vec3 basePos  = glm::vec3(worldGame[3]);

        // vr camera
        XrPosef currPose = VRManager::instance().XR->GetRenderer()->m_layer3D.GetPose(s_currentEye);
        glm::fvec3 eyePos(currPose.position.x, currPose.position.y, currPose.position.z);
        glm::fquat eyeRot(currPose.orientation.w, currPose.orientation.x, currPose.orientation.y, currPose.orientation.z);

        glm::vec3 newPos = basePos + (baseRot * eyePos);
        glm::quat newRot = baseRot * eyeRot;

        glm::mat4 newWorldVR = glm::translate(glm::mat4(1.0f), newPos) * glm::mat4_cast(newRot);
        glm::mat4 newViewVR = glm::inverse(newWorldVR);
        glm::mat3x4 rowMajor = glm::transpose(newViewVR);

        camera.mtx.setLEMatrix(rowMajor);
        // glm::fvec3 newPos = glm::fvec3(newWorldVR[3]);
        camera.pos.x = newPos.x;
        camera.pos.y = newPos.y;
        camera.pos.z = newPos.z;

        // Set look-at point by offsetting position in view direction
        glm::vec3 viewDir = -glm::vec3(rowMajor[2]); // Forward direction is -Z in view space
        camera.at.x = newPos.x + viewDir.x;
        camera.at.y = newPos.y + viewDir.y;
        camera.at.z = newPos.z + viewDir.z;

        // Transform world up vector by new rotation
        glm::vec3 upDir = glm::vec3(rowMajor[1]); // Up direction is +Y in view space
        camera.up.x = upDir.x;
        camera.up.y = upDir.y;
        camera.up.z = upDir.z;

        writeMemory(cameraOut, &camera);
        hCPU->gpr[3] = cameraOut;
    }
    else {
        // Log::print("[ERROR!!!] Getting render camera (with LR: {:08X}): {}", hCPU->sprNew.LR, camera);
    }
}

/*
===============================================================================
LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT
Getting render projection (with LR: 0363B80C): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = true, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03409C30): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = true, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 033D9F0C): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 039A9480): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 039E0FF4): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 034010CC): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Capturing color for 3D layer
Capturing color for 3D layer
Capturing depth for 3D layer
Capturing depth for 3D layer
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Capturing color for 2D layer
Capturing color for 2D layer
Getting render projection (with LR: 03857284): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Updated settings!
[Meta XR Simulator][00101.522830][V][arvr\projects\openxr_simulator\src\sim_xrsession.cpp:459] frameIndex 10920: creationTime 101446.726ms, beginTime 101458.428ms, endTime 101522.823ms, PDT 101527.860ms, PDP 13.889ms, shouldRender 1
[Meta XR Simulator][00101.523094][V][arvr\projects\openxr_simulator\src\sim_xrapilayer_debug_window_base.cpp:213]   2 layers
[Meta XR Simulator][00101.523218][V][arvr\projects\openxr_simulator\src\sim_xrapilayer_debug_window_base.cpp:216]     layer 0: XR_TYPE_COMPOSITION_LAYER_QUAD
[Meta XR Simulator][00101.523314][V][arvr\projects\openxr_simulator\src\sim_xrapilayer_debug_window_base.cpp:216]     layer 1: XR_TYPE_COMPOSITION_LAYER_PROJECTION
[Meta XR Simulator][00101.523770][V][arvr\projects\openxr_simulator\src\session_capture\sim_session_capturer_gfx.cpp:117] openxr_simulator::SessionCaptureRecorderGfx::endFrame
[Meta XR Simulator][00101.523950][V][arvr\projects\openxr_simulator\src\sim_xrsession.cpp:260] xrWaitFrame: missed frame interval: previous interval=7304, current interval=7309, time diff = 77.220ms
[Meta XR Simulator][00101.527869][V][arvr\projects\openxr_simulator\src\session_capture\sim_session_capturer_gfx.cpp:95] openxr_simulator::SessionCaptureRecorderGfx::beginFrame
LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT LEFT
===============================================================================
===============================================================================
RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT
Getting render projection (with LR: 03409C30): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 033D9F0C): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 039A9480): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 039E0FF4): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 034010CC): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03857284): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Capturing color for 3D layer
Capturing color for 3D layer
Capturing depth for 3D layer
Capturing depth for 3D layer
Capturing color for 2D layer
Capturing color for 2D layer
Getting render projection (with LR: 03AEA958): near = 1, far = 25000, angle = 0.8726647, fovySin = 0.4226183, fovyCos = 0.9063078, fovyTan = 0.4663077, aspect = 1.7777778, offsetX = 0, offsetY = 0
dirty = false, deviceDirty = false, matrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=1.2], deviceMatrix = [a00=0.0, a01=0.0, a02=0.0, a03=0.0] [a10=2.1, a11=0.0, a12=0.0, a13=0.0] [a20=0.0, a21=-1.0, a22=-2.0, a23=0.0] [a30=0.0, a31=-1.0, a32=0.0, a33=0.0] devicePosture = 1067083660, deviceZOffset = 0, deviceZScale = 1
Updated settings!
RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT RIGHT
===============================================================================
*/

constexpr uint32_t seadOrthoProjection = 0x1027B5BC;
constexpr uint32_t seadPerspectiveProjection = 0x1027B54C;

// https://github.com/KhronosGroup/OpenXR-SDK/blob/858912260ca616f4c23f7fb61c89228c353eb124/src/common/xr_linear.h#L564C1-L632C2
// https://github.com/aboood40091/sead/blob/45b629fb032d88b828600a1b787729f2d398f19d/engine/library/modules/src/gfx/seadProjection.cpp#L166
static glm::mat4 calculateProjectionMatrix(float nearZ, float farZ, const XrFovf& fov) {
    float l = tanf(fov.angleLeft) * nearZ;
    float r = tanf(fov.angleRight) * nearZ;
    float b = tanf(fov.angleDown) * nearZ;
    float t = tanf(fov.angleUp) * nearZ;

    float invW = 1.0f / (r - l);
    float invH = 1.0f / (t - b);
    float invD = 1.0f / (farZ - nearZ);

    glm::mat4 col;
    col[0][0] = 2 * nearZ * invW;
    col[1][1] = 2 * nearZ * invH;
    col[0][2] = (r + l) * invW;
    col[1][2] = (t + b) * invH;
    col[2][2] = -(farZ + nearZ) * invD;
    col[2][3] = -(2 * farZ * nearZ) * invD;
    col[3][2] = -1.0f;
    col[3][3] = 0.0f;

    return glm::transpose(col);
}

void CemuHooks::hook_GetRenderProjection(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    BESeadProjection projection = {};
    readMemory(hCPU->gpr[3], &projection);

    if (projection.__vftable == seadPerspectiveProjection) {
        BESeadPerspectiveProjection perspectiveProjection = {};
        readMemory(hCPU->gpr[3], &perspectiveProjection);
        Log::print("Render Proj. (LR: {:08X}): {}", hCPU->sprNew.LR, perspectiveProjection);

        XrFovf currFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(s_currentEye);
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
        glm::fmat4 newDeviceMatrix = newMatrix;
        perspectiveProjection.matrix = newMatrix;

        // calculate device matrix
        float zScale = perspectiveProjection.deviceZScale.getLE(); // normally 1 or 0.5
        float zOffset = perspectiveProjection.deviceZOffset.getLE(); // normally 0 or 0.5

        newDeviceMatrix[2][0] *= zScale;
        newDeviceMatrix[2][1] *= zScale;
        newDeviceMatrix[2][2] = (newDeviceMatrix[2][2] + newDeviceMatrix[3][2] * zOffset) * zScale;
        newDeviceMatrix[2][3] = newDeviceMatrix[2][3] * zScale + newDeviceMatrix[3][3] * zOffset;

        perspectiveProjection.deviceMatrix = newDeviceMatrix;
        perspectiveProjection.dirty = true;
        perspectiveProjection.deviceDirty = true;

        writeMemory(hCPU->gpr[12], &perspectiveProjection);
        hCPU->gpr[3] = hCPU->gpr[12];
    }
}

void CemuHooks::hook_EndCameraSide(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;

    Log::print("{0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0} {0}", s_currentEye == OpenXR::EyeSide::LEFT ? "LEFT" : "RIGHT");
    Log::print("===============================================================================");
    Log::print("");
    // s_currentEye = hCPU->gpr[3] == 0 ? OpenXR::EyeSide::RIGHT : OpenXR::EyeSide::LEFT;
}


void CemuHooks::hook_UpdateCameraRotation(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    // Log::print("[{}] Updated camera rotation", s_currentCameraRotation.second == OpenXR::EyeSide::LEFT ? "left" : "right");

    data_VRCameraRotationOut updatedCameraMatrix = {};
    updatedCameraMatrix.enabled = true;
    updatedCameraMatrix.rotX = s_currentCameraRotation.first.rotX;
    updatedCameraMatrix.rotY = s_currentCameraRotation.first.rotY;
    updatedCameraMatrix.rotZ = s_currentCameraRotation.first.rotZ;

    uint32_t ppc_cameraMatrixOffsetOut = hCPU->gpr[3];
    writeMemory(ppc_cameraMatrixOffsetOut, &updatedCameraMatrix);
}


void CemuHooks::hook_UpdateCameraOffset(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    // Log::print("[{}] Updated camera FOV and projection offset", s_currentEye == OpenXR::EyeSide::LEFT ? "left" : "right");

    XrFovf viewFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(s_currentEye);
    checkAssert(viewFOV.angleLeft <= viewFOV.angleRight, "OpenXR gave a left FOV that is larger than the right FOV! Behavior is unexpected!");
    checkAssert(viewFOV.angleDown <= viewFOV.angleUp, "OpenXR gave a top FOV that is larger than the bottom FOV! Behavior is unexpected!");

    data_VRProjectionMatrixOut projectionMatrix = calculateFOVAndOffset(viewFOV);

    data_VRCameraOffsetOut cameraOffsetOut = {
        .aspectRatio = projectionMatrix.aspectRatio,
        .fovY = projectionMatrix.fovY,
        .offsetX = projectionMatrix.offsetX,
        .offsetY = projectionMatrix.offsetY
    };
    uint32_t ppc_projectionMatrixOut = hCPU->gpr[11];
    writeMemory(ppc_projectionMatrixOut, &cameraOffsetOut);
}


void CemuHooks::hook_CalculateCameraAspectRatio(PPCInterpreter_t* hCPU) {
    hCPU->instructionPointer = hCPU->sprNew.LR;
    // Log::print("[{}] Updated camera aspect ratio", s_currentEye == OpenXR::EyeSide::LEFT ? "left" : "right");

    XrFovf viewFOV = VRManager::instance().XR->GetRenderer()->m_layer3D.GetFOV(s_currentEye);
    checkAssert(viewFOV.angleLeft <= viewFOV.angleRight, "OpenXR gave a left FOV that is larger than the right FOV! Behavior is unexpected!");
    checkAssert(viewFOV.angleDown <= viewFOV.angleUp, "OpenXR gave a top FOV that is larger than the bottom FOV! Behavior is unexpected!");

    data_VRProjectionMatrixOut projectionMatrix = calculateFOVAndOffset(viewFOV);

    data_VRCameraAspectRatioOut cameraOffsetOut = {
        .aspectRatio = projectionMatrix.aspectRatio,
        .fovY = projectionMatrix.fovY
    };
    uint32_t ppc_projectionMatrixOut = hCPU->gpr[28];
    writeMemory(ppc_projectionMatrixOut, &cameraOffsetOut);
}
