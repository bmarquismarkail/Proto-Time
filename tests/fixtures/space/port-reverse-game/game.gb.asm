.MEMORYMAP
 DEFAULTSLOT 0
 SLOTSIZE $4000
 SLOT 0 $0000
 SLOT 1 $4000
.ENDME
.ROMBANKMAP
 BANKSTOTAL 4
 BANKSIZE $4000
 BANKS 4
.ENDRO
.EMPTYFILL $ff
.INCLUDE "state.inc"
.BANK 0 SLOT 0
.ORG $40
Irq:
 push af
 push hl
 ld a,1
 ld (FLAG),a
 ld hl,FRAME
 inc (hl)
 jr nz,IrqDone
 inc hl
 inc (hl)
IrqDone:
 pop hl
 pop af
 reti
IrqEnd:
.ORG $100
Entry:
 nop
 jp Init
EntryEnd:
.ORG $104
Header:
.DB $ce,$ed,$66,$66,$cc,$0d,$00,$0b,$03,$73,$00,$83,$00,$0c,$00,$0d
.DB $00,$08,$11,$1f,$88,$89,$00,$0e,$dc,$cc,$6e,$e6,$dd,$dd,$d9,$99
.DB $bb,$bb,$67,$63,$6e,$0e,$ec,$cc,$dd,$dc,$99,$9f,$bb,$b9,$33,$3e
.DB "SPACE PORT",0,0,0,0,0,0
.DB 0,0,0,1,1,0,1,0,0,0,0,0
HeaderEnd:
.ORG $150
Init:
 di
 ld sp,$dff0
 xor a
 ldh ($40),a
 ldh ($26),a
 ld hl,X
 ld b,$25
ClearState:
 ld (hl),a
 inc hl
 dec b
 jr nz,ClearState
 ld a,1
 ld (BANKNOW),a
 ld ($2000),a
 ld a,$e4
 ldh ($47),a
 ldh ($48),a
 ld a,$80
 ldh ($26),a
 ld a,$77
 ldh ($24),a
 ld a,$22
 ldh ($25),a
 ld hl,Art
 ld de,$8000
 ld bc,160
LoadArt:
 ld a,(hl)
 inc hl
 ld (de),a
 inc de
 dec bc
 ld a,b
 or c
 jr nz,LoadArt
 ld hl,$9800
 ld bc,1024
FillMap:
 ld a,2
 ld (hl),a
 inc hl
 dec bc
 ld a,b
 or c
 jr nz,FillMap
 ld hl,$fe00
 ld b,160
 xor a
ClearOam:
 ld (hl),a
 inc hl
 dec b
 jr nz,ClearOam
 ld hl,Level
 ld de,$9840
 ld b,16
MapRows:
 ld c,20
MapColumns:
 ld a,(hl)
 inc hl
 ld (de),a
 inc de
 dec c
 jr nz,MapColumns
 ld a,e
 add a,12
 ld e,a
 jr nc,MapNext
 inc d
MapNext:
 dec b
 jr nz,MapRows
 call ResetGame
 call Render
 ld a,$93
 ldh ($40),a
 xor a
 ldh ($0f),a
 ld a,1
 ld ($ffff),a
 ld (READY),a
 ei
Main:
 halt
 nop
 ld a,(FLAG)
 or a
 jr z,Main
 xor a
 ld (FLAG),a
 call ReadInput
 call CallBank1
 call SoundTick
 call Render
 ld a,(FRAME)
 ld (COMMIT),a
 ld a,(FRAME+1)
 ld (COMMIT+1),a
 jp Main
InitEnd:

CallBank1:
 push af
 ld a,(BANKNOW)
 push af
 ld a,1
 ld (BANKNOW),a
 ld ($2000),a
 call Tick
 pop af
 ld (BANKNOW),a
 ld ($2000),a
 pop af
 ret
CallBank1End:
CallBank2:
 push af
 ld a,(BANKNOW)
 push af
 ld a,2
 ld (BANKNOW),a
 ld ($2000),a
 call Attempt
 pop af
 ld (BANKNOW),a
 ld ($2000),a
 pop af
 ret
CallBank2End:
ResetGame:
 ld a,(INPUT)
 push af
 ld hl,X
 ld b,12
 xor a
ResetBytes:
 ld (hl),a
 inc hl
 dec b
 jr nz,ResetBytes
 ld (BLOCKED),a
 ld (COLLECTED),a
 ld (VICTORY),a
 ld (LASTCUE),a
 ld (SOUND),a
 ld a,1
 ld (X),a
 ld (Y),a
 pop af
 ld (INPUT),a
 ret
ResetGameEnd:
ReadInput:
 ld a,$20
 ldh ($00),a
 ldh a,($00)
 cpl
 and $0f
 ld b,a
 ld a,$10
 ldh ($00),a
 ldh a,($00)
 cpl
 and 8
 swap a
 or b
 ld (INPUT),a
 ret
ReadInputEnd:
Render:
 ld a,(Y)
 add a,a
 add a,a
 add a,a
 add a,32
 ld ($fe00),a
 ld a,(X)
 add a,a
 add a,a
 add a,a
 add a,8
 ld ($fe01),a
 ld a,(POSE)
 ld ($fe02),a
 xor a
 ld ($fe03),a
 ld a,(PICKED)
 ld b,a
 ld hl,$9864
 ld a,4
 bit 0,b
 jr z,DrawOne
 ld a,2
DrawOne:
 ld (hl),a
 ld hl,$98e4
 ld a,4
 bit 1,b
 jr z,DrawTwo
 ld a,2
DrawTwo:
 ld (hl),a
 ld hl,$98e8
 ld a,4
 bit 2,b
 jr z,DrawThree
 ld a,2
DrawThree:
 ld (hl),a
 ld a,(SCORE)
 cp 3
 ld a,5
 jr nz,DrawExit
 ld a,6
DrawExit:
 ld ($98ea),a
 ld hl,$9801
 ld a,(SCORE)
 ld b,a
 ld c,3
HudLoop:
 ld a,b
 or a
 ld a,7
 jr z,HudEmpty
 ld a,8
 dec b
HudEmpty:
 ld (hl),a
 inc hl
 dec c
 jr nz,HudLoop
 ld a,(WON)
 or a
 ld a,2
 jr z,DrawWin
 ld a,9
DrawWin:
 ld ($9806),a
 ret
RenderEnd:
SoundTick:
 ld a,(CUE)
 or a
 jr z,SoundAdvance
 ld (LASTCUE),a
 ld (SOUND),a
 xor a
 ld (CUE),a
 ld (NOTE),a
 ld a,(SOUND)
 cp 2
 jr z,BlockedTone
 cp 3
 jr z,VictoryTone
 ld a,8
 ld (REMAIN),a
 call Tone880
 jr SoundAdvance
BlockedTone:
 ld a,4
 ld (REMAIN),a
 call Tone440
 jr SoundAdvance
VictoryTone:
 ld a,24
 ld (REMAIN),a
 call Tone880
SoundAdvance:
 ld a,(REMAIN)
 or a
 jr z,SoundOff
 dec a
 ld (REMAIN),a
 jr z,SoundOff
 ld b,a
 ld a,(SOUND)
 cp 3
 ret nz
 ld a,b
 cp 16
 jr z,NextNote
 cp 8
 ret nz
 ld a,2
 ld (NOTE),a
 jp Tone1319
NextNote:
 ld a,1
 ld (NOTE),a
 jp Tone1046
SoundOff:
 xor a
 ldh ($17),a
 ret
Tone440:
 ld a,$d6
 ld b,6
 jp Tone
Tone880:
 ld a,$6b
 ld b,7
 jp Tone
Tone1046:
 ld a,$83
 ld b,7
 jp Tone
Tone1319:
 ld a,$9d
 ld b,7
Tone:
 push af
 ld a,$80
 ldh ($16),a
 ld a,$f0
 ldh ($17),a
 pop af
 ldh ($18),a
 ld a,b
 or $80
 ldh ($19),a
 ret
SoundTickEnd:
Art:
.INCBIN "art.gb.bin"
ArtEnd:
Level:
.INCBIN "level.bin"
LevelEnd:
.INCLUDE "gameplay.gb.inc"
