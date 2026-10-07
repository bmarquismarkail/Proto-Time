; Reverse proof: Game Gear-authored indexed/shadow state path.
.BANK 1 SLOT 1
.ORG 0
Tick:
 ex af,af'
 exx
 ld a,(iy+$07) ; INPUT
 ld b,a
 and $80
 jr z,NoReset
 ld a,(ix+$08) ; PREV
 and $80
 jr nz,NoReset
 call ResetGame
 jr StoreInput
NoReset:
 ld a,(ix+$04) ; WON
 or a
 jr z,CheckDirection
 xor a
 ld (ix+$05),a ; POSE
 jr StoreInput
CheckDirection:
 ld a,b
 and $0f
 jr nz,Moving
 xor a
 ld (ix+$05),a ; POSE
 ld (ix+$06),a ; PHASE
 jr StoreInput
Moving:
 ld a,(ix+$06) ; PHASE
 inc a
 and 7
 ld (ix+$06),a ; PHASE
 jr nz,StoreInput
 ld a,(ix+$00) ; X
 ld (ix+$13),a ; NX
 ld a,(ix+$01) ; Y
 ld (ix+$14),a ; NY
 ld a,b
 and 1
 jr z,TryLeft
 ld a,(ix+$13) ; NX
 inc a
 ld (ix+$13),a ; NX
 jr MoveReady
TryLeft:
 ld a,b
 and 2
 jr z,TryDown
 ld a,(ix+$13) ; NX
 dec a
 ld (ix+$13),a ; NX
 jr MoveReady
TryDown:
 ld a,b
 and 8
 jr z,TryUp
 ld a,(ix+$14) ; NY
 inc a
 ld (ix+$14),a ; NY
 jr MoveReady
TryUp:
 ld a,(ix+$14) ; NY
 dec a
 ld (ix+$14),a ; NY
MoveReady:
 call CallBank2
StoreInput:
 ld a,(iy+$07) ; INPUT
 ld (ix+$08),a ; PREV
 exx
 ex af,af'
 ret
TickEnd:

.BANK 2 SLOT 1
.ORG 0
Attempt:
 ld a,(ix+$14) ; NY
 ld hl,Level
 ld de,20
 or a
 jr z,RowReady
Rows:
 add hl,de
 dec a
 jr nz,Rows
RowReady:
 ld a,(ix+$13) ; NX
 ld e,a
 ld d,0
 add hl,de
 ld a,(hl)
 cp 3
 jp z,BlockedMove
 cp 5
 jr nz,Allowed
 ld a,(ix+$03) ; SCORE
 cp 3
 jp nz,BlockedMove
Allowed:
 ld a,(ix+$13) ; NX
 ld (ix+$00),a ; X
 ld a,(ix+$14) ; NY
 ld (ix+$01),a ; Y
 ld a,(ix+$05) ; POSE
 xor 1
 ld (ix+$05),a ; POSE
 ld a,(ix+$01) ; Y
 cp 1
 jr nz,ItemTwo
 ld a,(ix+$00) ; X
 cp 4
 jr nz,CheckExit
 ld b,1
 jr Pick
ItemTwo:
 cp 5
 jr nz,CheckExit
 ld a,(ix+$00) ; X
 cp 4
 jr nz,ItemThree
 ld b,2
 jr Pick
ItemThree:
 cp 8
 jr nz,CheckExit
 ld b,4
Pick:
 ld a,(ix+$02) ; PICKED
 and b
 jr nz,CheckExit
 ld a,(ix+$02) ; PICKED
 or b
 ld (ix+$02),a ; PICKED
 ld a,(ix+$03) ; SCORE
 inc a
 ld (ix+$03),a ; SCORE
 ld a,1
 ld (ix+$09),a ; CUE
 ld a,(ix+$21) ; COLLECTED
 inc a
 ld (ix+$21),a ; COLLECTED
CheckExit:
 ld a,(ix+$00) ; X
 cp 10
 ret nz
 ld a,(ix+$01) ; Y
 cp 5
 ret nz
 ld a,(ix+$03) ; SCORE
 cp 3
 ret nz
 ld a,1
 ld (ix+$04),a ; WON
 ld a,3
 ld (ix+$09),a ; CUE
 ld a,(ix+$22) ; VICTORY
 inc a
 ld (ix+$22),a ; VICTORY
 ret
BlockedMove:
 xor a
 ld (ix+$05),a ; POSE
 ld a,2
 ld (ix+$09),a ; CUE
 ld a,(ix+$20) ; BLOCKED
 inc a
 ld (ix+$20),a ; BLOCKED
 ret
AttemptEnd:
