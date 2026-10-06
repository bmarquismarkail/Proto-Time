.BANK 1 SLOT 1
.ORG 0
Tick:
 ld a,(INPUT)
 ld b,a
 and $80
 jr z,NoReset
 ld a,(PREV)
 and $80
 jr nz,NoReset
 call ResetGame
 jr StoreInput
NoReset:
 ld a,(WON)
 or a
 jr z,CheckDirection
 xor a
 ld (POSE),a
 jr StoreInput
CheckDirection:
 ld a,b
 and $0f
 jr nz,Moving
 xor a
 ld (POSE),a
 ld (PHASE),a
 jr StoreInput
Moving:
 ld a,(PHASE)
 inc a
 and 7
 ld (PHASE),a
 jr nz,StoreInput
 ld a,(X)
 ld (NX),a
 ld a,(Y)
 ld (NY),a
 ld a,b
 and 1
 jr z,TryLeft
 ld a,(NX)
 inc a
 ld (NX),a
 jr MoveReady
TryLeft:
 ld a,b
 and 2
 jr z,TryDown
 ld a,(NX)
 dec a
 ld (NX),a
 jr MoveReady
TryDown:
 ld a,b
 and 8
 jr z,TryUp
 ld a,(NY)
 inc a
 ld (NY),a
 jr MoveReady
TryUp:
 ld a,(NY)
 dec a
 ld (NY),a
MoveReady:
 call CallBank2
StoreInput:
 ld a,(INPUT)
 ld (PREV),a
 ret
TickEnd:

.BANK 2 SLOT 1
.ORG 0
Attempt:
 ld a,(NY)
 ld hl,Level
 ld de,20
 or a
 jr z,RowReady
Rows:
 add hl,de
 dec a
 jr nz,Rows
RowReady:
 ld a,(NX)
 ld e,a
 ld d,0
 add hl,de
 ld a,(hl)
 cp 3
 jp z,BlockedMove
 cp 5
 jr nz,Allowed
 ld a,(SCORE)
 cp 3
 jp nz,BlockedMove
Allowed:
 ld a,(NX)
 ld (X),a
 ld a,(NY)
 ld (Y),a
 ld a,(POSE)
 xor 1
 ld (POSE),a
 ld a,(Y)
 cp 1
 jr nz,ItemTwo
 ld a,(X)
 cp 4
 jr nz,CheckExit
 ld b,1
 jr Pick
ItemTwo:
 cp 5
 jr nz,CheckExit
 ld a,(X)
 cp 4
 jr nz,ItemThree
 ld b,2
 jr Pick
ItemThree:
 cp 8
 jr nz,CheckExit
 ld b,4
Pick:
 ld a,(PICKED)
 and b
 jr nz,CheckExit
 ld a,(PICKED)
 or b
 ld (PICKED),a
 ld a,(SCORE)
 inc a
 ld (SCORE),a
 ld a,1
 ld (CUE),a
 ld a,(COLLECTED)
 inc a
 ld (COLLECTED),a
CheckExit:
 ld a,(X)
 cp 10
 ret nz
 ld a,(Y)
 cp 5
 ret nz
 ld a,(SCORE)
 cp 3
 ret nz
 ld a,1
 ld (WON),a
 ld a,3
 ld (CUE),a
 ld a,(VICTORY)
 inc a
 ld (VICTORY),a
 ret
BlockedMove:
 xor a
 ld (POSE),a
 ld a,2
 ld (CUE),a
 ld a,(BLOCKED)
 inc a
 ld (BLOCKED),a
 ret
AttemptEnd:
