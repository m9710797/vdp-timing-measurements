; TODO NTSC or PAL

wrtvdp	equ	#0047	; B = value ; C = VDP-register
rg0sav	equ	#F3DF
rg1sav	equ	#F3E0
rg8sav	equ	#FFE7

	db	#fe
	dw	begin
	dw	end
	dw	exec
	

	org	#c000

begin:

mode:	db	0	; screen mode N = [0..9]  (9 = screen0, width 80)
ena:	db	0	; 0,2 -> screen disabled
			; 1   -> screen enabled, sprites disabled
			; 3   -> screen enabled, sprites enabled
spr:	db	0	; 0 ->  8x 8 sprites, not magnified
			; 1 ->  8x 8 sprites, magnified
			; 2 -> 16x16 sprites, not magnified
			; 3 -> 16x16 sprites, magnified

	ds	#c008-$

wtlin:	dw	0	; number of lines to wait after VBLANK to start command
wtcnt:	dw	0	; number of extra loops to wait
nacc	dw	0	; number of CPU-VRAM accesses
acct	db	0	; access-type 0=read 1=write

	ds	#c010-$

cmddat	dw	0,0	; sx, sy
	dw	0,0	; dx, dy
	dw	0,0	; nx, ny
	db	0,0,0	; col, arg, cmd

regv:	db	#04,#70,#1F,#7F	;  0- 3
	db	#1F,#AF,#1B,#F4	;  4- 7
	db	#08,#04,#04,#01	;  8-11
	db	#00,#00,#00,#00	; 12-15
	db	#00,#18,#00,#00	; 16-19
	db	#00,#3b,#05,#00	; 20-23
	db	#00,#00,#00,#00	; 24-27
	db	#00,#00,#00,#00	; 28-31
nreg:	equ	$-regv

modreg:	db	#00,#10	; screen 0, width 40
	db	#00,#00	; screen 1
	db	#02,#00	; screen 2
	db	#00,#08	; screen 3
	db	#04,#00	; screen 4
	db	#06,#00	; screen 5
	db	#08,#00	; screen 6
	db	#0A,#00	; screen 7
	db	#0E,#00	; screen 8
	db	#04,#10	; screen 0, width 80 (=9)

spatt1:	db	#00,#00,#00,#00	;  0  Y, X, pat, EC|000|color
	db	#00,#10,#04,#01	;  1        y=208 -> skip lower prio sprites
	db	#00,#20,#08,#02	;  2
	db	#00,#30,#0C,#03	;  3
	db	#00,#40,#10,#04	;  4
	db	#00,#50,#14,#05	;  5
	db	#00,#60,#18,#06	;  6
	db	#00,#70,#1C,#07	;  7
	db	#00,#80,#20,#08	;  8
	db	#00,#90,#24,#09	;  9
	db	#00,#A0,#28,#0A	; 10
	db	#00,#B0,#2C,#0B	; 11
	db	#00,#C0,#30,#0C	; 12
	db	#00,#D0,#34,#0D	; 13
	db	#00,#E0,#38,#0E	; 14
	db	#00,#F0,#3C,#0F	; 15
	db	#80,#00,#40,#00	; 16   <- possibly y=208
	db	#10,#80,#44,#01	; 17
	db	#20,#80,#48,#02	; 18
	db	#20,#70,#4C,#03	; 19
	db	#30,#80,#50,#04	; 20
	db	#30,#70,#54,#05	; 21
	db	#30,#60,#58,#06	; 22
	db	#30,#50,#5C,#07	; 23
	db	#40,#80,#60,#08	; 24
	db	#40,#70,#64,#09	; 25
	db	#40,#60,#68,#0A	; 26
	db	#40,#50,#6C,#0B	; 27
	db	#40,#40,#70,#0C	; 28
	db	#40,#30,#74,#0D	; 29
	db	#40,#20,#78,#0E	; 30
	db	#40,#10,#7C,#0F	; 31

spatt2:	db	#00,#00,#00,#00	;  0  Y, X, pat, reserved
	db	#00,#10,#04,#00	;  1        y=216 -> skip lower prio sprites
	db	#00,#20,#08,#00	;  2
	db	#00,#30,#0C,#00	;  3
	db	#00,#40,#10,#00	;  4
	db	#00,#50,#14,#00	;  5
	db	#00,#60,#18,#00	;  6
	db	#00,#70,#1C,#00	;  7
	db	#00,#80,#20,#00	;  8
	db	#00,#90,#24,#00	;  9
	db	#00,#A0,#28,#00	; 10
	db	#00,#B0,#2C,#00	; 11
	db	#00,#C0,#30,#00	; 12
	db	#00,#D0,#34,#00	; 13
	db	#00,#E0,#38,#00	; 14
	db	#00,#F0,#3C,#00	; 15
	db	#80,#00,#40,#00	; 16   <- possibly y=216
	db	#10,#80,#44,#00	; 17
	db	#20,#80,#48,#00	; 18
	db	#20,#70,#4C,#00	; 19
	db	#30,#80,#50,#00	; 20
	db	#30,#70,#54,#00	; 21
	db	#30,#60,#58,#00	; 22
	db	#30,#50,#5C,#00	; 23
	db	#40,#80,#60,#00	; 24
	db	#40,#70,#64,#00	; 25
	db	#40,#60,#68,#00	; 26
	db	#40,#50,#6C,#00	; 27
	db	#40,#40,#70,#00	; 28
	db	#40,#30,#74,#00	; 29
	db	#40,#20,#78,#00	; 30
	db	#40,#10,#7C,#00	; 31
	
	ds	#c200-$
exec:
	jp	init
	jp	inireg
	jp	setmod
	jp	setena
	jp	setspr
	jp	vrmpat
	jp	vrmspr
	jp	docmd



; Initialize all
init:
	call	inireg
	call	setmod
	call	setena
	call	setspr
	call	vrmpat
	call	vrmspr
	call	docmd
	ret


; Initialize VDP registers
inireg:
	di
	ld	hl,regv
	ld	c,0
iloop:	ld	b,(hl)
	inc	hl
	push	bc
	push	hl
	call	wrtvdp		; also writes to RAM
	pop	hl
	pop	bc
	inc	c
	ld	a,c
	cp	nreg
	jr	c,iloop
	ei
	ret


; Set screen mode
setmod:
	ld	a,(mode)
	ld	c,a
	ld	b,0
	ld	hl,modreg
	add	hl,bc
	add	hl,bc
	ld	a,(rg0sav)
	and	#F1
	or	(hl)
	ld	b,a
	inc	hl
	ld	c,0
	push	hl
	call	wrtvdp		; R#0
	pop	hl
	ld	a,(rg1sav)
	and	#E7
	or	(hl)
	ld	b,a
	ld	c,1
	call	wrtvdp		; R#1
	ei
	ret


; Set screen/sprites enabled/disabled
setena:	di
	ld	a,(ena)
	and	1
	ld	a,(rg1sav)
	jr	nz,sc_ena
sc_dis:	and	#BF
	jr	sc_set
sc_ena	or	#40
sc_set	ld	b,a
	ld	c,1
	call	wrtvdp
	ld	a,(ena)
	and	2
	ld	a,(rg8sav)
	jr	nz,sp_ena
sp_dis:	or	#02
	jr	sp_set
sp_ena	and	#FD
sp_set	ld	b,a
	ld	c,8
	call	wrtvdp
	ei
	ret


; Set sprite properties (8x8 or 16x16, magnified or not)
setspr:	di
	ld	a,(spr)
	and	#03
	ld	b,a
	ld	a,(rg1sav)
	and	#FC
	or	b
	ld	b,a
	ld	c,1
	call	wrtvdp

	ld	bc,#8080
	ld	a,(spr)
	and	4
	jr	z,spry
	ld	bc,208*256+216
spry	ld	a,b
	ld	(spatt1+4*16),a
	ld	a,c
	ld	(spatt2+4*16),a
	ei
	ret


; Initialize VRAM with a known pattern
vrmpat:
	di
	ld	bc,0*256+14
	call	wrtvdp
	di
	ld	a,0
	out	(#99),a
	ld	a,#40		; write
	out	(#99),a

	ld	hl,0
flp1:	ld	a,h
	or	l
	out	(#98),a
	inc	hl
	ld	a,h
	or	l
	jr	nz,flp1

	ld	hl,#ffff
flp2	ld	a,h
	or	l
	out	(#98),a
	jr	z,elp2
	dec	hl
	jr	flp2
elp2
	ei
	ret


; Initialize sprite tables
vrmspr:	di

	; sprite attribute table
	;  0xD400-0xD5FF : color attributes (mode 2)
	;  0xD600-0xD67F : attributes (mode 2)
	;  0xD780-0xD7FF : attributes (mode 1)
	ld	bc,3*256+14	; A16-14
	call	wrtvdp
	di
	ld	a,0
	out	(#99),a		; A7-A0
	ld	a,#54
	out	(#99),a		; A13-A8 or #40

	; #D400-#D5FF,  color attributes
	;  16 bytes per sprite (x 32 sprites)
	;  each byte:  EC|CC|IC|0|color
	ld	c,32
cattl2:	ld	a,c
	and	15
	ld	b,16
cattl1:	out	(#98),a
	djnz	cattl1
	dec	c
	ld	a,c
	or	a
	jr	nz,cattl2

	; #D600-#D67F,  sprite attributes, mode 2
	ld	hl,spatt2
	ld	bc,128*256+#98
	otir

	; #D680-#D77F,  not used
	ld	b,0
	ld	a,0
dattl1	out	(#98),a
	djnz	dattl1
	
	; #D780-#D7FF,  sprite attributes, mode 1
	ld	hl,spatt1
	ld	bc,128*256+#98
	otir
	
	; 0xD800-0xDFFF,  sprite patterns
	ld	bc,3*256+14	; A16-14
	call	wrtvdp
	di
	ld	a,0
	out	(#99),a		; A7-A0
	ld	a,#58
	out	(#99),a		; A13-A8 or #40

	ld	bc,#800/2
patl:	ld	a,#55
	out	(#98),a
	ld	a,#AA
	out	(#98),a
	dec	bc
	ld	a,b
	or	c
	jr	nz,patl
	
	ei
	ret


; Execute a VDP command
docmd:	di
	in	a,(#AA)
	and	#f0
	or	8
	out	(#AA),a	; select keyboard matrix row 8

	ld	a,2
	out	(#99),a
	ld	a,15+128
	out	(#99),a	; select S#2
	ex	(sp),hl
	ex	(sp),hl

busy0:	in	a,(#99)
	rrca
	jr	c,busy0

cmdlp:	
	ld	a,5
	out	(#99),a
	ld	a,14+128
	out	(#99),a	; select CPU-VRAM range 0x14000-0x17FFF
	ld	a,(acct)
	or	a
	jr	nz,setwr
setrd:	ld	a,0
	out	(#99),a
	ld	a,0
	out	(#99),a	; select read from 0x14000
	jr	setend
setwr:	ld	a,0
	out	(#99),a
	ld	a,#60
	out	(#99),a	; select write to 0x16000
setend:

	ld	a,32
	out	(#99),a
	ld	a,17+128
	out	(#99),a	; select indirect register access

	ld	hl,(wtlin)
	ld	de,(wtcnt)
	ld	bc,64*256+32

ver1:	in	a,(#99)
	and	b
	jr	nz,ver1
ver2	in	a,(#99)
	and	b
	jr	z,ver2
	; at this point VR has just become 1,
	; so we just entered VBLANK

	; wait for [HL] number of lines
	ld	a,h
	or	l
	jr	z,hor3
hor0:	dec	hl
hor1:	in	a,(#99)
	and	c
	jr	nz,hor1
hor2:	in	a,(#99)
	and	c
	jr	z,hor2
	; at this point HR has just become 1
	ld	a,h
	or	l
	jr	nz,hor0
hor3:

	ld	hl,cmddat
	ld	bc,15*256+#9b

	inc	de
delay:	dec	de
	ld	a,d
	or	e
	jr	nz,delay

	otir		; finally start the command

	ld	hl,(nacc)
	ld	a,h
	or	l
	jp	z,accend
	ld	a,(acct)
	or	a
	jr	nz,accwr

	; A display line takes 1368 VDP cycles (228 Z80 cycles).
	; A 'IN A,(n)' or 'OUT (n),A' instruction takes 12 Z80 cycles.
	; So it's possible to do _exactly_ 19 such instructions per line.
	; The instruction is repeated 40x to make sure we cover at least
	; one full line.

accrd:	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	in	a,(#98)
	dec	hl
	ld	a,h
	or	l
	jr	nz,accrd
	jr	accend

accwr:	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	out	(#98),a
	dec	hl
	ld	a,h
	or	l
	jr	nz,accwr

accend:
busy:	in	a,(#99)
	rrca
	jr	c,busy

	in	a,(#a9)
	inc	a
	jp	z,cmdlp	; any key (in this row) pressed?

	xor	a
	out	(#99),a
	ld	a,15+128
	out	(#99),a	; select S#0
	ei
	ret


end:	equ	$
