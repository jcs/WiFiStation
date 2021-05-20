; vim:syntax=z8a:ts=8
;
; WiFiStation
; DataFlash loader
;
; Copyright (c) 2019-2021 joshua stein <jcs@jcs.org>
;
; Permission to use, copy, modify, and distribute this software for any
; purpose with or without fee is hereby granted, provided that the above
; copyright notice and this permission notice appear in all copies.
;
; THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
; WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
; MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
; ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
; WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
; ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
; OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
;

	.module	dataflashloader

	; locations relative to v2.54 firmware
	.equ	p2shadow,		#0xdba2
	.equ	delay_func,		#0x0a5c

	; which page (Yahoo! menu slot) to save an uploaded program to
	; page 0 is 0x0000, page 1 0x4000, page 2, 0x8000, etc.
	.equ	DATAFLASH_PAGE,		#0

	; when running from WSLoader, we are loaded at 0x8000 and use slot 4
	.equ	SLOT_ADDR,		#0x4000
	.equ	RUN_ADDR,		#0x8000
	.equ	SLOT_DEVICE,		#0x6
	.equ	SLOT_PAGE,		#0x5

	; where we'll buffer the 256 bytes we receive before writing to flash
	.equ	RAM_ADDR,		#0xd000

	.equ	SDP_LOCK,		#SLOT_ADDR + 0x040a
	.equ	SDP_UNLOCK,		#SLOT_ADDR + 0x041a

	.equ	CONTROL_DIR,		#0x0a
	.equ	CONTROL_DIR_OUT,	#0xff
	.equ	CONTROL_DIR_IN,		#0

	.equ	CONTROL_PORT,		#0x9
	.equ	CONTROL_STROBE,		#(1 << 0)
	.equ	CONTROL_LINEFEED,	#(1 << 1)
	.equ	CONTROL_INIT,		#(1 << 2)
	.equ	CONTROL_SELECT,		#(1 << 3)

	.equ	DATA_DIR,		#0x2c
	.equ	DATA_DIR_OUT,		#0xff
	.equ	DATA_DIR_IN,		#0
	.equ	DATA_PORT,		#0x2d

	.equ	STATUS_PORT,		#0x21
	.equ	STATUS_BUSY,		#(1 << 7)
	.equ	STATUS_ACK,		#(1 << 6)
	.equ	STATUS_PAPEROUT,	#(1 << 5)


	.area	_DATA
	.area	_HEADER (ABS)
	.org 	#RUN_ADDR

	jp	main

 	.dw	(icons)
 	.dw	(caption)
 	.dw	(dunno)
dunno:
	.db	#0
zip:
	.dw	#0
zilch:
	.dw	#0
caption:
	.dw	#0x0001
	.dw	(endcap - caption - 6)	; num of chars
	.dw	#0x0006			; offset to first char
	.ascii	"FlashLoader"		; the caption string
endcap:

icons:
	.dw	#0x0			; size icon0
	.dw	(icon0 - icons)		; offset to icon0
	.dw	#0			; size icon1
	.dw	(icon1 - icons)		; offset to icon1 (0x00b5)
icon0:
	.dw	#0x0			; icon width
	.db	#0x0			; icon height

icon1:
	.dw	#0			; icon width
	.db	#0			; icon height

; enables 'new mail' light when a is 1, else disables
new_mail:
	di
	cp	#0
	jr	z, light_off
light_on:
	ld	a, (p2shadow)
	set	4, a
	jr	write_p2
light_off:
	ld	a, (p2shadow)
	res	4, a
write_p2:
	ld	(p2shadow), a
	out	(#0x02), a		; write p2shadow to port2
	ei
	ret

delay:
	push	af
	push	bc
	push	hl
	call	#delay_func
	pop	hl
	pop	bc
	pop	af
	ret

; get transfer size bytes, then read a flash sector into ram, write it, repeat
main:
	push	ix
	ld	ix, #0
	add	ix, sp
	push	bc
	push	de
	push	hl
	ld	hl, #5000
	push	hl
	call	delay			; wait 5 seconds before starting in
	pop	hl			;  case wifistation is rebooting
	ld	a, #CONTROL_DIR_OUT
	out	(#CONTROL_DIR), a
	xor	a
	out	(#CONTROL_PORT), a
	ld	a, #DATA_DIR_IN
	out	(#DATA_DIR), a		; we're going to be receiving
	ld	a, #1			; enable 'new mail' light
	call	new_mail
	ld	hl, #-8			; stack bytes for local storage
	add	hl, sp
	ld	sp, hl
	in	a, (#SLOT_DEVICE)
 	ld	-3(ix), a		; stack[-3] = slot 8 device
	in	a, (#SLOT_PAGE)
 	ld	-4(ix), a		; stack[-4] = slot 8 device
	ld	a, #3			; slot 8 device = dataflash
	out	(#SLOT_DEVICE), a
	xor	a			; slot 8 page = 0
	out	(#SLOT_PAGE), a
	ld	hl, #SLOT_ADDR
	ld	-5(ix), h
	ld	-6(ix), l		; stack[-5,-6] = data flash location
get_size:
	call	lptrecv_blocking	; low byte of total bytes to download
	ld	-8(ix), a
	call	lptrecv_blocking	; high byte of total bytes to download
	ld	-7(ix), a
	cp	#0x40			; we can't write more than 0x3fff
	jr	c, size_ok
size_too_big:
	jp	0x0000			; *shrug*
size_ok:
	di				; prevent things from touching RAM
	call	sdp
	ld	a, (#SDP_UNLOCK)
read_chunk_into_ram:
	; read 256 bytes at a time into ram, erase the target flash sector,
	; then program it with those bytes
	ld	b, -7(ix)
	ld	c, -8(ix)
	ld	hl, #RAM_ADDR
ingest_loop:
	call	lptrecv_blocking
	ld	(hl), a
	inc	hl
	dec	bc
	ld	a, l
	cp	#0
	jr	z, done_ingesting	; on 256-byte boundary
	ld	a, b
	cp	#0
	jr	nz, ingest_loop		; bc != 0, keep reading input
	ld	a, c
	cp	#0
	jr	nz, ingest_loop		; bc != 0, keep reading input
done_ingesting:
	ld	-7(ix), b		; update bytes remaining to fetch on
	ld	-8(ix), c		;  next iteration
move_into_flash:
	ld	a, #3			; slot 8 device = dataflash
	out	(#SLOT_DEVICE), a
	ld	a, #DATAFLASH_PAGE
	out	(#SLOT_PAGE), a
	ld	de, #RAM_ADDR
	ld	h, -5(ix)		; data flash write location
	ld	l, -6(ix)
sector_erase:
	ld	(hl), #0x20		; 28SF040 Sector-Erase Setup
	ld	(hl), #0xd0		; 28SF040 Execute
sector_erase_wait:
	ld	a, (hl)			; wait until End-of-Write
	ld	b, a
	ld	a, (hl)
	cp	b
	jr	nz, sector_erase_wait
byte_program_loop:
	ld	a, (de)
	ld	(hl), #0x10		; 28SF040 Byte-Program Setup
	ld	(hl), a			; 28SF040 Execute
byte_program:
	ld	a, (hl)
	ld	b, a
	ld	a, (hl)			; End-of-Write by reading it
	cp	b
	jr	nz, byte_program	; read until writing succeeds
	inc	hl			; next flash byte
	inc	de			; next RAM byte
	ld	a, l
	cp	#0
	jr	nz, byte_program_loop
sector_done:
	ld	-5(ix), h		; update data flash write location
	ld	-6(ix), l
	ld	a, -7(ix)
	cp	#0
	jp	nz, read_chunk_into_ram	; more data to transfer
	ld	a, -8(ix)
	cp	#0
	jp	nz, read_chunk_into_ram	; more data to transfer
	; all done
flash_out:
	ld	a, #3			; slot 8 device = dataflash
	out	(#SLOT_DEVICE), a
	xor	a			; slot 8 page = 0
	out	(#SLOT_PAGE), a
	call	sdp
	ld	a, (#SDP_LOCK)
bail:
 	ld	a, -3(ix)		; restore slot 8 device
	out	(#SLOT_DEVICE), a
 	ld	a, -4(ix)		; restore slot 8 page
	out	(#SLOT_PAGE), a
	ld	hl, #8			; remove stack bytes
	add	hl, sp
	ld	sp, hl
	pop	hl
	pop	de
	pop	bc
	ld	sp, ix
	pop	ix
	ei
	jp	0x0000			; reset when we're done


sdp:
	ld	a, (#SLOT_ADDR + 0x1823) ; 28SF040 Software Data Protection
	ld	a, (#SLOT_ADDR + 0x1820)
	ld	a, (#SLOT_ADDR + 0x1822)
	ld	a, (#SLOT_ADDR + 0x0418)
	ld	a, (#SLOT_ADDR + 0x041b)
	ld	a, (#SLOT_ADDR + 0x0419)
	ret
	; caller needs to read final SDP_LOCK or SDP_UNLOCK address


; return byte in a
lptrecv_blocking:
	push	hl
wait_for_busy:
	in	a, (#STATUS_PORT)
	and	#STATUS_BUSY		; is busy high? (strobe on writer)
	jr	z, wait_for_busy	; no, wait until it is
	and	#STATUS_ACK		; but is ack high too?  that's bogus
	jr	nz, wait_for_busy
	ld	a, #CONTROL_LINEFEED	; raise linefeed
	out	(#CONTROL_PORT), a
wait_for_busy_ack:
	in	a, (#STATUS_PORT)
	and	#STATUS_BUSY		; is busy high?
	jr	nz, wait_for_busy_ack	; no, wait
read_data:
	in	a, (#DATA_PORT)
	ld	l, a
lower_lf:
	xor	a
	out	(#CONTROL_PORT), a	; lower linefeed
	ld	a, l			; return read byte in a
	pop	hl
	ret
