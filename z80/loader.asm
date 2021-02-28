; vim:syntax=z8a:ts=8
;
; WiFiStation
; parallel port loader
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

	.module	loader

	.area	_DATA
	.area	_HEADER (ABS)
	.org 	0x4000			; we're running from dataflash

	jp	main

 	.dw	(icons)
 	.dw	(caption)
 	.dw	(dunno)
dunno:
	.db	#0
	.dw	#0
	.dw	#0
caption:
	.dw	#0x0001
	.dw	(endcap - caption - 6)
	.dw	#0x0006
	.ascii	"WLoader"		; the icon caption
endcap:

icons:
	.dw	#0			; size icon0
	.dw	(icon0 - icons)		; offset to icon0
	.dw	#0			; size icon1
	.dw	(icon1 - icons)		; offset to icon1 (0x00b5)
icon0:
	.dw	#0			; icon width
	.db	#0			; icon height
icon1:
	.dw	#0			; icon width
	.db	#0			; icon height

	; actual loader code

	.equ	CONTROL_DIR,		#0x0a
	.equ	CONTROL_DIR_OUT,	#0xff
	.equ	CONTROL_DIR_IN,		#0

	.equ	CONTROL_PORT,		#0x9
	.equ	CONTROL_STROBE_BIT,	#0
	.equ	CONTROL_STROBE,		#(1 << CONTROL_STROBE_BIT)
	.equ	CONTROL_LINEFEED_BIT,	#1
	.equ	CONTROL_LINEFEED,	#(1 << CONTROL_LINEFEED_BIT)
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

main:
	; lower control lines (but they are inverted so write 1s)
	ld	a, #CONTROL_DIR_OUT
	out	(#CONTROL_DIR), a
	ld	a, #0xff
	out	(#CONTROL_PORT), a

	; first read the low and high bytes of the length we're going to read
	call	lptrecv_blocking
	ld	l, a
	call	lptrecv_blocking
	ld	h, a		; hl = bytes to download

	ld	a, #1		; put ram page 1 into slot8000
	out	(#0x08), a
	out	(#0x07), a

	ld	bc, #0x8000	; bc = ram addr
getbyte:
	call	lptrecv_blocking
	ld	(bc), a
	inc	bc		; addr++
	dec	hl		; bytes--
	xor	a
	or	h
	jr	nz, getbyte	; if h != 0, keep reading
	xor	a
	or	l
	jr	nz, getbyte	; if l != 0, keep going
	jp	0x8000		; else, jump to new code in ram


; at idle, lower all control lines (which are inverted, so write 0s)
;
; writer:				reader:
; raise strobe
;					see high strobe as high busy
;					raise linefeed
; see high linefeed as high ack
; write all data pins
; lower strobe
;					see low strobe as low busy
;					read data
;					lower linefeed
; see lower linefeed as high ack

; return byte in a
lptrecv_blocking:
	push	hl
wait_for_busy:
	in	a, (#STATUS_PORT)
	and	#STATUS_BUSY		; is busy high? (strobe on writer)
	jr	z, wait_for_busy	; no, wait until it is
	ld	a, #DATA_DIR_IN
	out	(#DATA_DIR), a		; we're receiving
	ld	a, #0xff
	res	#CONTROL_LINEFEED_BIT, a ; raise inverted linefeed (write 0)
	out	(#CONTROL_PORT), a
wait_for_busy_ack:
	in	a, (#STATUS_PORT)
	and	#STATUS_BUSY		; is busy low?
	jr	nz, wait_for_busy_ack	; no, wait
read_data:
	in	a, (#DATA_PORT)
	ld	l, a
lower_lf:
	; if we're echoing bytes back during testing, we can skip this write
	; since lptsend_blocking will do it for us
	ld	a, #0xff
	out	(#CONTROL_PORT), a	; lower inverted linefeed (write 1)
;echo:
;	call	lptsend_blocking	; confirmation echo byte
	ld	a, l
	pop	hl
	ret

; ; send byte in l
; lptsend_blocking:
; 	push	hl
; 	ld	a, #DATA_DIR_OUT
; 	out	(#DATA_DIR), a		; we're sending out
; 	ld	a, #0xff
; 	res	#CONTROL_STROBE_BIT, a
; 	out	(#CONTROL_PORT), a	; raise inverted strobe (write 0)
; wait_for_ack:
; 	in	a, (#STATUS_PORT)
; 	and	#STATUS_ACK		; is ack high?
; 	jr	z, wait_for_ack		; no, wait
; got_ack:
; 	ld	a, l
; 	out	(#DATA_PORT), a		; write data
; 	ld	a, #0xff
; 	out	(#CONTROL_PORT), a	; lower inverted strobe (write 1)
; wait_for_final_ack:
; 	in	a, (#STATUS_PORT)
; 	and	#STATUS_ACK		; is ack high?
; 	jr	z, wait_for_final_ack	; no, wait
; got_final_ack:
; 	pop	hl
; 	ret
