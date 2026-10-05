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
.ORG 0
Entry:
 di
 jp Init
EntryEnd:
.ORG $38
Irq:
 push af
 push hl
 in a,($bf)
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
 ei
 reti
IrqEnd:
.ORG $100
Init:
 di
 im 1
 ld sp,$dff0
 xor a
 ld ($fffd),a
 ld hl,X
 ld b,$25
ClearState:
 ld (hl),a
 inc hl
 djnz ClearState
 ld a,1
 ld (BANKNOW),a
 ld ($fffe),a
 ld a,2
 ld ($ffff),a
 ld a,$ff
 out ($06),a
 ld a,$9f
 out ($7f),a
 ld a,$bf
 out ($7f),a
 ld a,$df
 out ($7f),a
 ld a,$ff
 out ($7f),a
 ld hl,Registers
 ld b,11
 ld c,$80
SetRegisters:
 ld a,(hl)
 inc hl
 out ($bf),a
 ld a,c
 out ($bf),a
 inc c
 djnz SetRegisters
 ld hl,$4000
 call SetVram
 ld bc,$4000
ClearVram:
 xor a
 out ($be),a
 dec bc
 ld a,b
 or c
 jr nz,ClearVram
 ld hl,$c000
 call SetVram
 ld hl,Palette
 ld b,64
LoadPalette:
 ld a,(hl)
 inc hl
 out ($be),a
 djnz LoadPalette
 ld hl,$4000
 call SetVram
 ld hl,Art
 ld bc,320
LoadArt:
 ld a,(hl)
 inc hl
 out ($be),a
 dec bc
 ld a,b
 or c
 jr nz,LoadArt
 ld hl,$78cc
 call SetVram
 ld b,52
ClearHud:
 ld a,2
 out ($be),a
 xor a
 out ($be),a
 djnz ClearHud
 ld hl,Level
 ld de,$794c
 ld b,16
MapRows:
 push hl
 push bc
 ld h,d
 ld l,e
 call SetVram
 pop bc
 pop hl
 ld c,20
MapColumns:
 ld a,(hl)
 inc hl
 out ($be),a
 xor a
 out ($be),a
 dec c
 jr nz,MapColumns
 ld a,e
 add a,64
 ld e,a
 jr nc,MapNext
 inc d
MapNext:
 djnz MapRows
 ld hl,$7f01
 call SetVram
 ld a,$d0
 out ($be),a
 call ResetGame
 call Render
 ld a,$60
 out ($bf),a
 ld a,$81
 out ($bf),a
 in a,($bf)
 ld a,1
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
SetVram:
 ld a,l
 out ($bf),a
 ld a,h
 out ($bf),a
 ret
SetVramEnd:
CallBank1:
 push af
 ld a,(BANKNOW)
 push af
 ld a,1
 ld (BANKNOW),a
 ld ($fffe),a
 call Tick
 pop af
 ld (BANKNOW),a
 ld ($fffe),a
 pop af
 ret
CallBank1End:
CallBank2:
 push af
 ld a,(BANKNOW)
 push af
 ld a,2
 ld (BANKNOW),a
 ld ($fffe),a
 call Attempt
 pop af
 ld (BANKNOW),a
 ld ($fffe),a
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
 in a,($dc)
 cpl
 and $0f
 ld c,a
 ld b,0
 bit 3,c
 jr z,NotRight
 set 0,b
NotRight:
 bit 2,c
 jr z,NotLeft
 set 1,b
NotLeft:
 bit 0,c
 jr z,NotUp
 set 2,b
NotUp:
 bit 1,c
 jr z,NotDown
 set 3,b
NotDown:
 in a,($00)
 cpl
 and $80
 or b
 ld (INPUT),a
 ret
ReadInputEnd:
Render:
 ld hl,$7f00
 call SetVram
 ld a,(Y)
 add a,a
 add a,a
 add a,a
 add a,39
 out ($be),a
 ld hl,$7f80
 call SetVram
 ld a,(X)
 add a,a
 add a,a
 add a,a
 add a,48
 out ($be),a
 ld a,(POSE)
 out ($be),a
 ld a,(PICKED)
 ld b,a
 ld hl,$7994
 ld a,4
 bit 0,b
 jr z,DrawOne
 ld a,2
DrawOne:
 call WriteTile
 ld hl,$7a94
 ld a,4
 bit 1,b
 jr z,DrawTwo
 ld a,2
DrawTwo:
 call WriteTile
 ld hl,$7a9c
 ld a,4
 bit 2,b
 jr z,DrawThree
 ld a,2
DrawThree:
 call WriteTile
 ld a,(SCORE)
 cp 3
 ld a,5
 jr nz,DrawExit
 ld a,6
DrawExit:
 ld hl,$7aa0
 call WriteTile
 ld hl,$78ce
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
 call WriteTile
 inc hl
 inc hl
 dec c
 jr nz,HudLoop
 ld a,(WON)
 or a
 ld a,2
 jr z,DrawWin
 ld a,9
DrawWin:
 ld hl,$78d8
 call WriteTile
 ret
WriteTile:
 push af
 call SetVram
 pop af
 out ($be),a
 xor a
 out ($be),a
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
 ld a,$9f
 out ($7f),a
 ret
Tone440:
 ld a,$ee
 ld b,15
 jp Tone
Tone880:
 ld a,$ff
 ld b,7
 jp Tone
Tone1046:
 ld a,$eb
 ld b,6
 jp Tone
Tone1319:
 ld a,$e5
 ld b,5
Tone:
 and $0f
 or $80
 out ($7f),a
 ld a,b
 out ($7f),a
 ld a,$90
 out ($7f),a
 ret
SoundTickEnd:
Registers:
.DB $04,$00,$0e,$ff,$07,$7e,$00,$00,$00,$00,$ff
RegistersEnd:
Palette:
.DB $ff,$0f,$aa,$0a,$55,$05,$00,$00,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
.DB $ff,$0f,$aa,$0a,$55,$05,$00,$00,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
PaletteEnd:
Art:
.INCBIN "art.gg.bin"
ArtEnd:
Level:
.INCBIN "level.bin"
LevelEnd:
.INCLUDE "gameplay.gg.inc"
.BANK 1 SLOT 1
.ORG $3ff0
Header:
.DB "TMR SEGA",0,0,0,0,0,0,0,$6e
HeaderEnd:
