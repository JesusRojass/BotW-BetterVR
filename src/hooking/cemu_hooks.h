#pragma once
#include "rendering/openxr.h"

class CemuHooks {
public:
    CemuHooks() {
        m_cemuHandle = GetModuleHandleA(NULL);
        checkAssert(m_cemuHandle != NULL, "Failed to get handle of Cemu process which is required for interfacing with Cemu!");

        gameMeta_getTitleId = (gameMeta_getTitleIdPtr_t)GetProcAddress(m_cemuHandle, "gameMeta_getTitleId");
        memory_getBase = (memory_getBasePtr_t)GetProcAddress(m_cemuHandle, "memory_getBase");
        osLib_registerHLEFunction = (osLib_registerHLEFunctionPtr_t)GetProcAddress(m_cemuHandle, "osLib_registerHLEFunction");
        checkAssert(gameMeta_getTitleId != NULL && memory_getBase != NULL && osLib_registerHLEFunction != NULL, "Failed to get function pointers of Cemu functions! Is this hook being used on Cemu?");

        bool isSupportedTitleId = gameMeta_getTitleId() == 0x00050000101C9300 || gameMeta_getTitleId() == 0x00050000101C9400 || gameMeta_getTitleId() == 0x00050000101C9500;
        checkAssert(isSupportedTitleId, std::format("Expected title IDs for Breath of the Wild (00050000-101C9300, 00050000-101C9400 or 00050000-101C9500) but received {:16x}!", gameMeta_getTitleId()).c_str());

        s_memoryBaseAddress = (uint64_t)memory_getBase();
        checkAssert(s_memoryBaseAddress != 0, "Failed to get memory base address of Cemu process!");

        osLib_registerHLEFunction("coreinit", "hook_UpdateSettings", &hook_UpdateSettings);
        osLib_registerHLEFunction("coreinit", "hook_UpdateCameraPositionAndTarget", &hook_UpdateCameraPositionAndTarget);
        osLib_registerHLEFunction("coreinit", "hook_UpdateCameraRotation", &hook_UpdateCameraRotation);
        osLib_registerHLEFunction("coreinit", "hook_UpdateCameraOffset", &hook_UpdateCameraOffset);
        osLib_registerHLEFunction("coreinit", "hook_CalculateCameraAspectRatio", &hook_CalculateCameraAspectRatio);
        osLib_registerHLEFunction("coreinit", "hook_CreateNewScreen", &hook_CreateNewScreen);
        osLib_registerHLEFunction("coreinit", "hook_UpdateActorList", &hook_UpdateActorList);
        osLib_registerHLEFunction("coreinit", "hook_CreateNewActor", &hook_CreateNewActor);
    };
    ~CemuHooks() {
        FreeLibrary(m_cemuHandle);
    };

    static data_VRSettingsIn GetSettings();
    static uint32_t GetFramesSinceLastCameraUpdate() { return s_framesSinceLastCameraUpdate.load(); }

private:
    HMODULE m_cemuHandle;

    osLib_registerHLEFunctionPtr_t osLib_registerHLEFunction;
    memory_getBasePtr_t memory_getBase;
    gameMeta_getTitleIdPtr_t gameMeta_getTitleId;

    static uint64_t s_memoryBaseAddress;
    static std::atomic_uint32_t s_framesSinceLastCameraUpdate;

    static void hook_UpdateSettings(PPCInterpreter_t* hCPU);
    static void hook_UpdateCameraPositionAndTarget(PPCInterpreter_t* hCPU);
    static void hook_UpdateCameraRotation(PPCInterpreter_t* hCPU);
    static void hook_UpdateCameraOffset(PPCInterpreter_t* hCPU);
    static void hook_CalculateCameraAspectRatio(PPCInterpreter_t* hCPU);
    static void hook_CreateNewScreen(PPCInterpreter_t* hCPU);
    static void hook_UpdateActorList(PPCInterpreter_t* hCPU);
    static void hook_CreateNewActor(PPCInterpreter_t* hCPU);
    static void updateFrames();

    static void writeMemoryBE(uint64_t offset, Matrix34* mtxPtr) {
        writeMemoryBE(offset + offsetof(Matrix34, x_x), &mtxPtr->x_x);
        writeMemoryBE(offset + offsetof(Matrix34, x_y), &mtxPtr->x_y);
        writeMemoryBE(offset + offsetof(Matrix34, x_z), &mtxPtr->x_z);
        writeMemoryBE(offset + offsetof(Matrix34, y_x), &mtxPtr->y_x);
        writeMemoryBE(offset + offsetof(Matrix34, y_y), &mtxPtr->y_y);
        writeMemoryBE(offset + offsetof(Matrix34, y_z), &mtxPtr->y_z);
        writeMemoryBE(offset + offsetof(Matrix34, z_x), &mtxPtr->z_x);
        writeMemoryBE(offset + offsetof(Matrix34, z_y), &mtxPtr->z_y);
        writeMemoryBE(offset + offsetof(Matrix34, z_z), &mtxPtr->z_z);
        writeMemoryBE(offset + offsetof(Matrix34, pos_x), &mtxPtr->pos_x);
        writeMemoryBE(offset + offsetof(Matrix34, pos_y), &mtxPtr->pos_y);
        writeMemoryBE(offset + offsetof(Matrix34, pos_z), &mtxPtr->pos_z);
    }

    static void readMemoryBE(uint64_t offset, Matrix34* mtxPtr) {
        readMemoryBE(offset + offsetof(Matrix34, x_x), &mtxPtr->x_x);
        readMemoryBE(offset + offsetof(Matrix34, x_y), &mtxPtr->x_y);
        readMemoryBE(offset + offsetof(Matrix34, x_z), &mtxPtr->x_z);
        readMemoryBE(offset + offsetof(Matrix34, y_x), &mtxPtr->y_x);
        readMemoryBE(offset + offsetof(Matrix34, y_y), &mtxPtr->y_y);
        readMemoryBE(offset + offsetof(Matrix34, y_z), &mtxPtr->y_z);
        readMemoryBE(offset + offsetof(Matrix34, z_x), &mtxPtr->z_x);
        readMemoryBE(offset + offsetof(Matrix34, z_y), &mtxPtr->z_y);
        readMemoryBE(offset + offsetof(Matrix34, z_z), &mtxPtr->z_z);
        readMemoryBE(offset + offsetof(Matrix34, pos_x), &mtxPtr->pos_x);
        readMemoryBE(offset + offsetof(Matrix34, pos_y), &mtxPtr->pos_y);
        readMemoryBE(offset + offsetof(Matrix34, pos_z), &mtxPtr->pos_z);
    }

    template <typename T>
    static void writeMemoryBE(uint64_t offset, T* valuePtr) {
        *valuePtr = swapEndianness(*valuePtr);
        memcpy((void*)(s_memoryBaseAddress + offset), (void*)valuePtr, sizeof(T));
    }

    template <typename T>
    static void writeMemory(uint64_t offset, T* valuePtr) {
        memcpy((void*)(s_memoryBaseAddress + offset), (void*)valuePtr, sizeof(T));
    }

    template <typename T>
    static void readMemoryBE(uint64_t offset, T* resultPtr) {
        uint64_t memoryAddress = s_memoryBaseAddress + offset;
        memcpy(resultPtr, (void*)memoryAddress, sizeof(T));
        *resultPtr = swapEndianness(*resultPtr);
    }

    template <typename T>
    static void readMemory(uint64_t offset, T* resultPtr) {
        uint64_t memoryAddress = s_memoryBaseAddress + offset;
        memcpy(resultPtr, (void*)memoryAddress, sizeof(T));
    }
};
