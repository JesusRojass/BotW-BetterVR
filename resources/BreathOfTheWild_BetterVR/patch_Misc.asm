[BetterVR_Misc_V208]
moduleMatches = 0x6267BFD0

.origin = codecave

; the game normally draws most of the UI on top of the 3D color buffer
; the VR mod uses the alpha channel of the 2D UI as a mask to present a transparent version of the HUD on top of the 3D framebuffer
; this patch basically fixes a few elements not using the right blending options
; for example, the minimap, dialogue boxes and various other things to overwrite the alpha of previously rendered 2D elements
; which cause gaps inside the image that's shown in VR
hook_FixUIBlending:
stwu r1, -0x10(r1)
mflr r0
stw r0, 0x14(r1)
stw r3, 0xC(r1)
stw r4, 0x8(r1)

bl import.coreinit.hook_FixUIBlending

bl import.gx2.GX2SetBlendControl

lwz r4, 0x8(r1)
lwz r3, 0xC(r1)
lwz r0, 0x14(r1)
addi r1, r1, 0x10
mtlr r0
blr

0x03C58BF8 = bla hook_FixUIBlending
0x03C58CC0 = bla hook_FixUIBlending
0x03C58D68 = bla hook_FixUIBlending

; forces the player to always be guarding when the shield is drawn
; uses player rotation to determine the guard direction, not shield direction
;0x02D79F50 = li r3, 1

; forces StartShiekSensorGaugeDemo to instantly finish and not get softlocked (which happens when you unlock a tower)
; this disables the gauge animation where it would get stuck in
; not ideal but better than softlocking
0x023F90E0 = nop
0x023F90E4 = nop
0x023F90EC = nop
0x023F910C = nop

; ==================================================================================
; make jump always jump twice as high to compensate for increased player gravity due to bug
;0x02CA2464 = bla import.coreinit.hook_OverwriteCameraParam


; player move speed, doesn't affect gravity
;0x101E55F8 = .float 3.0


; Disable jump button setting from in-game options
; todo: test whether this is actually working

;0x024AA7C4 = ksys_gdt_getFlag_JumpButtonChange:
;0x024AA7C4 = li r3, 0
;0x024AA7C8 = blr

; disables all collisions (from camera presumably) ; EDIT: this description seems wrong, it's actually used in some camera stuff?
;0x030E47CC = li r3, 0 ; this prevents a jump to Actor::m56. Might be trying to get the actor's shouldRender flag or smth, and we are patching that?
;0x030E47E4 = li r3, 0 ; this does nothing, since its the same as the instruction it replaces: li r3, 0

; working VR physics swinging
;0x024AA8F4 = nop
;0x024AA878 = nop

;0x024AA7C4 = li r0, 1
;0x024B6274 = li r3, 1


;0x024AA7D0 = nop
;0x024AA7E0 = nop
;0x024AA89C = nop


; force activeAttackSensor to always be true
;0x024AA8FC = li r12, 1

;0x024adee8 = li r10, 0x0
;0x024adef0 = nop ; sth r10, 0xA14(r30)
;0x24ac60c = li r8, 0x0
;0x024ac614 = nop ; sth r8, 0xA14(r30)
;0x24aa9f8 = li r12, 0x0
;0x24aaa00 = nop ; sth r12, 0xA14(r31)
;0x024AE6E4 = li r12, 0x0
;0x24ae6ec = nop ; sth r12, 0xA14(r31)

; READ weaponFlags
;0x024ADEDC = li r0, 0x400
;0x24b621c = li r0, 0x400
;0x24ac608 = li r8, 0x400
;0x24aa9f8 = li r12, 0x400
;0x24aca30 = li r7, 0x400
;0x249f274 = li r10, 0x400
;0x24ae6d4 = li r12, 0x400
;0x24afd0c = li r9, 0x400
;0x24b2f0c = li r10, 0x400

;0x24ae748 = li r11, 0xA16
;0x249f7bc = li r0, 0xA16

; New VR physics
;0x24ad870 = nop
;0x24ad8fc = nop


; writes 0xA16, so can be ignored:
;0x24ad924 = nop

; 0x24ae6f8 = li r12, 0
;0x24ae700 = nop ; sth r12, 0xA16(r31)
; 0x24ae750 = li r11, 0
;0x24ae758 = nop ; sth r11, 0xA16(r31)


; this forces the model bind function to never try to bind it to a specific bone of the actor
; 0x31258A4 = jumpLocation:
; 0x0312578C = b jumpLocation

; remove binded weapon
; 0x03125880 = nop


;0x020661B8 = cmpwi r1, 0
;0x024AC5B0 = nop
;0x024AC588 = nop
;0x024AC8C4 = nop
;0x024AC8D0 = nop
;0x024AC8D4 = nop
;0x024AC8DC = cmpw r3, r3
;0x024AC7B4 = nop

;0x02C18754 = nop
;0x02C18764 = nop

;0x034B69C0 = li r0, 1

;0x02C196A4 = li r3, 1

;0x024B6F40 = li r12, 0