[BetterVR_Misc_V208]
moduleMatches = 0x6267BFD0

.origin = codecave

0x101BF8DC = .float 1.0

; disables the opacity fade effect when it gets near the camera
0x02C05A2C = cmpw r3, r3
; disables the opacity fade effect when it gets near any graphics
;0x02C05A2C = cmpw r3, r3

; disables camera collision fading when it collides with objects
;0x02C07848 = cmpwi r1, 0

; 0x101C0084 = .float 0.0
; 0x02C0E38C = cmpw r3, r3


0x10216594 = .float $cameraDistance

; this forces the model bind function to never try to bind it to a specific bone of the actor
; 0x31258A4 = jumpLocation:
; 0x0312578C = b jumpLocation

; remove binded weapon
; 0x03125880 = nop