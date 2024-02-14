[BetterVR_NewActorHook_V208]
moduleMatches = 0x6267BFD0

.origin = codecave


spawnString:
.string "Weapon_Sword_018"
.int 0 ; not necessary?

0x10463708 = CreatePlayerEquipActorMgr__sInstance:
0x10263910 = sead__SafeString__vt:

0x02A15998 = requestCreateWeapon:

vr_spawnEquipment:
; function epilogue
stwu r1, -0x30(r1)
stw r0, 0x20(r1)
stw r3, 0x1C(r1)
stw r4, 0x18(r1)
stw r5, 0x14(r1)
stw r6, 0x10(r1)
; safestring vtable
; safestring string ptr (also the thing that gets passed to functions I think?)
; safestring nullptr maybe?
mflr r3
stw r3, 0x24(r1)


; r3 is spawnee?
lis r3, CreatePlayerEquipActorMgr__sInstance@ha
lwz r3, CreatePlayerEquipActorMgr__sInstance@l(r3)
; r4 is the weapon/armor type?
li r4, 0
; r5 is the actor name
lis r5, sead__SafeString__vt@ha
addi r5, r5, sead__SafeString__vt@l
stw r5, 0x08(r1)

lis r5, spawnString@ha
addi r5, r5, spawnString@l
stw r5, 0x04(r1)

addi r5, r1, 0x04
; r6 is the weapon's life
li r6, 1
; r7 is the weapons's modifier. Can be kept at 0 for none.
li r7, 0

bl requestCreateWeapon

; function prologue
lwz r6, 0x10(r1)
lwz r5, 0x14(r1)
lwz r4, 0x18(r1)
lwz r3, 0x1C(r1)
lwz r0, 0x20(r1)
mtlr r3
lwz r3, 0x24(r1)
addi r1, r1, 0x30
blr

vr_createActorHook:
stw r7, 0x20(r28) ; original instruction

; function epilogue
mflr r0
stwu r1, -0x0C(r1)
stw r0, 0x10(r1)
stw r5, 0x08(r1)
stw r6, 0x04(r1)

; r0 is the offset within an Actor class to the ListNode item
; r7 is the actor list size
; r4 is the ListNode within the just created actor
; r3 is the actor list ptr
; r30 is the newly created actor
addi r3, r28, 0x18 ; this->actorList ptr (holds ptr to first ListNode?)
lwz r0, 0x24(r28)

li r5, 0
mr r6, r3

startOfLoop:
subf r6, r0, r6 ; is now current iterator's actor ptr
bl import.coreinit.hook_UpdateActorList
add r6, r6, r0 ; is now current iterator's ListNode ptr

addi r6, r6, 0x04 ; is now next iterator's ListNode ptr
lwz r6, 0x00(r6) ; is now next iterator's actor ptr
addi r5, r5, 1
cmpw r5, r7
blt startOfLoop

; function prologue
lwz r6, 0x04(r1)
lwz r5, 0x08(r1)
lwz r0, 0x10(r1)
mtlr r0
addi r1, r1, 0x0C

mr r3, r31 ; repeat original instruction
blr


0x037B7698 = bla vr_createActorHook