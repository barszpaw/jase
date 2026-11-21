; fsrv_fdd3000.asm -- CP/M 2.2 file server for the FDD3000 station, ported
; from bosman-8's fsrv.c v2.4 (https://github.com/tapiau/bosman-8,
; software/tools/fsrv/fsrv.c) -- protocol/BDOS logic translated line-by-line
; from that C source (read, not guessed: GET/PUT/DIR/QUIT command bytes,
; per-record CRC8+ACK lockstep for GET/PUT, FCB/wildcard handling in
; build_fcb, directory-entry wire format in send_dir_entry/send_dir_end --
; see tools/fsrv/README.md for exactly which facts came from where).
;
; ONE deliberate, user-directed change from the original: the two inline-asm
; I/O primitives (reader/punch/reader_ready in fsrv.c) targeted bosman-8's
; real hardware, a Zilog SIO-B at ports 0x81/0x83. FDD3000 has no SIO-B --
; this targets the station's own WD2123 (src/Wd2123.h) CHA channel instead
; (data=#80, status/cmd=#81, CONFIRMED port map -- see Wd2123.h's own class
; comment), enabled once at startup via a single command-register write
; (bit0=TX enable, bit2=RX enable). Every other routine (BDOS calls, FCB
; building, CRC, directory walk) is untouched CP/M 2.2 logic, identical on
; any CP/M machine -- there was nothing Bosman-8-specific left to port there.
;
; SPEED (baud-rate change) is NOT implemented here, unlike the README's
; client: fsrv.c v2.4 itself doesn't implement it either (only D/G/P/Q are
; dispatched in its main loop), and WD2123's own byte-level TCP transport
; has no real baud concept to change in the first place (see Wd2123.h's
; class comment) -- nothing on either end for SPEED to control.
;
; Assembled with z80asm (Debian/Ubuntu package, NOT z88dk's z80asm --
; different tool, different syntax). Dot-prefixed "local" labels in this
; z80asm are scoped to the whole FILE, not to the enclosing routine (per
; `man z80asm`) -- so every label below has a routine-specific prefix
; instead of relying on scoping, deliberately, to avoid duplicate-label
; errors.
;
; Build: z80asm -o FSRV.COM fsrv_fdd3000.asm   (org 0100h below IS the
; whole file's first byte -- a CP/M .COM has no separate header).

	org 0100h

; ---------------------------------------------------------------------
; BDOS function numbers used (standard CP/M 2.2, not station-specific)
; ---------------------------------------------------------------------
BDOS_CONOUT:	equ 2
BDOS_WRITESTR:	equ 9
BDOS_CSTAT:	equ 11
BDOS_OPEN:	equ 15
BDOS_CLOSE:	equ 16
BDOS_SFIRST:	equ 17
BDOS_SNEXT:	equ 18
BDOS_DELETE:	equ 19
BDOS_READ:	equ 20
BDOS_WRITE:	equ 21
BDOS_MAKE:	equ 22
BDOS_DMAOFF:	equ 26
BDOS_FSIZE:	equ 35
BDOS_CREAD:	equ 1

; WD2123 CHA ports (CONFIRMED, src/Wd2123.h / src/FddStation.cpp RS1 decode)
WD_DATA:		equ 80h
WD_STATUS:	equ 81h
WD_RXREADY_BIT:	equ 01h
WD_TXREADY_BIT:	equ 02h
WD_ENABLE_TXRX:	equ 05h		; bit0 TX-enable | bit2 RX-enable

; ---------------------------------------------------------------------
; Entry point
; ---------------------------------------------------------------------
start:
	; Deliberately does NOT set its own SP: CCP already set one up before
	; CALLing this program (same convention standard small .COM utilities
	; rely on), and this program's own stack usage is modest (a handful
	; of nested CALLs, at most two PUSH HL frames at once). An earlier
	; version did "LD SP,stack_top" here, discarding CCP's real return
	; address off the stack entirely -- the final RET then popped two
	; zero bytes from the (uninitialized `defs 64`) stack area instead,
	; landing at address 0000h (the warm-boot vector) rather than back
	; in CCP. Found live: after QUIT, the station kept running (BDOS/BIOS
	; silently re-reading tracks 0-1 to reload CCP, matching a real warm
	; boot) instead of returning to a normal "A>" prompt.

	ld a,WD_ENABLE_TXRX
	out (WD_STATUS),a

	ld de,msg_banner
	ld c,BDOS_WRITESTR
	call 5

main_loop:
	call console_ready
	or a
	jr z,main_noconsole
	call console_in
	call to_upper
	cp 'Q'
	jr z,main_exit
main_noconsole:
	call reader_ready
	or a
	jr z,main_loop
	call reader
	cp 'D'
	jr z,main_dodir
	cp 'G'
	jr z,main_doget
	cp 'P'
	jr z,main_doput
	cp 'Q'
	jr z,main_doquit
	call send_error
	jr main_loop
main_dodir:
	call command_dir
	jr main_loop
main_doget:
	call command_get
	jr main_loop
main_doput:
	call command_put
	jr main_loop
main_doquit:
	call send_ok
main_exit:
	ld de,msg_exit
	ld c,BDOS_WRITESTR
	call 5
	ret

; ---------------------------------------------------------------------
; WD2123 CHA primitives (the only station-specific part of this file)
; ---------------------------------------------------------------------

; A = 0 if no byte waiting from the PC, nonzero if one is
reader_ready:
	in a,(WD_STATUS)
	and WD_RXREADY_BIT
	ret

; Blocking read -- returns the byte in A
reader:
reader_wait:
	in a,(WD_STATUS)
	and WD_RXREADY_BIT
	jr z,reader_wait
	in a,(WD_DATA)
	ret

; Blocking write of the byte in A. Uses C as scratch for the byte itself
; (status polling clobbers A) -- every call site below treats C as
; dead across a punch call, by convention, same as this routine does.
punch:
	ld c,a
punch_wait:
	in a,(WD_STATUS)
	and WD_TXREADY_BIT
	jr z,punch_wait
	ld a,c
	out (WD_DATA),a
	ret

; ---------------------------------------------------------------------
; Local console helpers (BDOS-standard, same on any CP/M machine --
; lets an operator type Q on whatever is wired to CONIN/CONST, e.g. a
; MonitorLink telnet session, to stop the server locally)
; ---------------------------------------------------------------------

; A = 0xFF if a key is waiting, 0x00 otherwise
console_ready:
	ld c,BDOS_CSTAT
	call 5
	ret

; Blocking read of one console character into A (BDOS C_READ also echoes)
console_in:
	ld c,BDOS_CREAD
	call 5
	ret

to_upper:
	cp 61h		; 'a'
	jr c,to_upper_done
	cp 7bh		; 'z'+1
	jr nc,to_upper_done
	sub 20h
to_upper_done:
	ret

; ---------------------------------------------------------------------
; Status replies -- single status byte over the wire (punch), plus a
; LOCAL-CONSOLE-ONLY text message (BDOS C_WRITESTR) for a human watching
; the station's own console. The text never reaches the PC client --
; confirmed against the real 'fsrv' Python client (tools/fsrv/README.md):
; it only ever reads the single status byte, never any trailing text.
; ---------------------------------------------------------------------

send_error:
	ld a,'E'
	call punch
	ld de,msg_err
	ld c,BDOS_WRITESTR
	call 5
	ret

send_ok:
	ld a,'K'
	call punch
	ld de,msg_ok
	ld c,BDOS_WRITESTR
	call 5
	ret

; ---------------------------------------------------------------------
; recv_name -- reads a null-terminated name from the wire into name_buf
; (max 15 chars kept, matching fsrv.c's 16-byte name_buf; any further
; wire bytes before the real null terminator are still consumed, never
; left desynchronising the stream, same as the original's do-while).
; ---------------------------------------------------------------------
recv_name:
	ld hl,name_buf
	ld b,15
recv_name_loop:
	call reader
	or a
	jr z,recv_name_null
	ld (hl),a
	inc hl
	djnz recv_name_loop
recv_name_drain:
	call reader
	or a
	jr nz,recv_name_drain
recv_name_null:
	ld (hl),0
	ret

; ---------------------------------------------------------------------
; build_fcb -- parses name_buf (optional "X:" drive prefix, then an 8.3
; name with '*'/'?' wildcards) into fcb[0..11]. Translated directly from
; fsrv.c's build_fcb() -- note the original's own `wildcard` parameter
; is unused there (always does the same '*'/'?' handling either way), so
; this version takes no parameter either.
; ---------------------------------------------------------------------
build_fcb:
	ld hl,fcb
	ld b,36
	xor a
build_fcb_clear:
	ld (hl),a
	inc hl
	djnz build_fcb_clear
	ld hl,fcb+1
	ld b,11
	ld a,' '
build_fcb_fill:
	ld (hl),a
	inc hl
	djnz build_fcb_fill

	ld hl,name_buf
	ld a,(hl)
	or a
	jr z,build_fcb_noprefix
	inc hl
	ld a,(hl)
	cp ':'
	jr nz,build_fcb_rewind
	dec hl
	ld a,(hl)
	call to_upper
	cp 'A'
	jr c,build_fcb_skipdrv
	cp 51h		; 'P'+1
	jr nc,build_fcb_skipdrv
	sub 40h		; 'A'-1
	ld (fcb),a
build_fcb_skipdrv:
	inc hl
	inc hl
	jr build_fcb_init
build_fcb_rewind:
	dec hl
build_fcb_noprefix:
build_fcb_init:
	ld c,1		; pos
	ld b,0		; ext flag
build_fcb_main:
	ld a,(hl)
	or a
	jr z,build_fcb_done
	inc hl
	call to_upper
	cp '.'
	jr nz,build_fcb_notdot
	ld b,1
	ld c,9
	jr build_fcb_main
build_fcb_notdot:
	cp '*'
	jr nz,build_fcb_notstar
build_fcb_starfill:
	ld a,b
	or a
	ld a,9
	jr z,build_fcb_starlimit
	ld a,12
build_fcb_starlimit:
	cp c
	jr z,build_fcb_main
	ld de,fcb
	ld a,e
	add a,c
	ld e,a
	jr nc,build_fcb_starnc
	inc d
build_fcb_starnc:
	ld a,'?'
	ld (de),a
	inc c
	jr build_fcb_starfill
build_fcb_notstar:
	push af
	ld a,b
	or a
	jr nz,build_fcb_extcheck
	ld a,c
	cp 9
	jr nc,build_fcb_drop
	jr build_fcb_store
build_fcb_extcheck:
	ld a,c
	cp 12
	jr nc,build_fcb_drop
build_fcb_store:
	ld de,fcb
	ld a,e
	add a,c
	ld e,a
	jr nc,build_fcb_storenc
	inc d
build_fcb_storenc:
	pop af
	ld (de),a
	inc c
	jr build_fcb_main
build_fcb_drop:
	pop af
	jr build_fcb_main
build_fcb_done:
	ret

; ---------------------------------------------------------------------
set_dma:
	ld de,dma
	ld c,BDOS_DMAOFF
	call 5
	ret

; ---------------------------------------------------------------------
; send_dir_entry -- A = directory slot (0-3) just returned by
; F_SFIRST/F_SNEXT. Punches the raw 11-byte name+ext (attribute bits
; masked off) from the current DMA buffer, same bytes the 'fsrv' client
; expects (tools/fsrv/README.md's DIR wire format).
; ---------------------------------------------------------------------
send_dir_entry:
	ld h,0
	ld l,a
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,hl
	add hl,hl
	ld de,dma+1
	add hl,de
	ld b,11
send_dir_entry_loop:
	ld a,(hl)
	and 7fh
	call punch
	inc hl
	djnz send_dir_entry_loop
	ret

send_dir_end:
	ld b,11
	xor a
send_dir_end_loop:
	call punch
	djnz send_dir_end_loop
	ret

; ---------------------------------------------------------------------
command_dir:
	call recv_name
	call build_fcb
	call set_dma
	ld de,fcb
	ld c,BDOS_SFIRST
	call 5
	cp 0ffh
	jr z,command_dir_end
command_dir_loop:
	call send_dir_entry
	call set_dma
	ld de,fcb
	ld c,BDOS_SNEXT
	call 5
	cp 0ffh
	jr nz,command_dir_loop
command_dir_end:
	call send_dir_end
	ld de,msg_ok
	ld c,BDOS_WRITESTR
	call 5
	ret

; ---------------------------------------------------------------------
; command_get -- GET: PC sends 'G'+name\0; we reply 'K'+rlo+rhi, then
; for each record: 128 data bytes + 1 XOR-CRC byte, waiting for a 'K'
; ack byte from the PC after every single record (lockstep, not a
; streamed block) before sending the next one, same as fsrv.c.
; ---------------------------------------------------------------------
command_get:
	call recv_name
	call build_fcb
	ld de,fcb
	ld c,BDOS_FSIZE
	call 5
	ld a,(fcb+33)
	ld l,a
	ld a,(fcb+34)
	ld h,a
	push hl
	call build_fcb
	ld de,fcb
	ld c,BDOS_OPEN
	call 5
	cp 0ffh
	jr nz,command_get_opened
	pop hl
	call send_error
	ret
command_get_opened:
	call set_dma
	ld a,'K'
	call punch
	pop hl
	ld a,l
	call punch
	ld a,h
	call punch
command_get_loop:
	ld a,h
	or l
	jr z,command_get_done
	ld de,fcb
	ld c,BDOS_READ
	call 5
	cp 0
	jr z,command_get_readok
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	call send_error
	ret
command_get_readok:
	push hl
	ld hl,dma
	ld b,128
	ld d,0
command_get_sendbyte:
	ld a,(hl)
	xor d
	ld d,a
	ld a,(hl)
	call punch
	inc hl
	djnz command_get_sendbyte
	ld a,d
	call punch
	call reader
	cp 'K'
	jr z,command_get_ackok
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	ld de,msg_ackerr
	ld c,BDOS_WRITESTR
	call 5
	pop hl
	ret
command_get_ackok:
	pop hl
	dec hl
	jr command_get_loop
command_get_done:
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	ld a,'K'
	call punch
	ld de,msg_ok
	ld c,BDOS_WRITESTR
	call 5
	ret

; ---------------------------------------------------------------------
; command_put -- PUT: PC sends 'P'+name\0+rlo+rhi, we delete any existing
; file, create a new one, ack 'K', then for each record: receive 128
; bytes + 1 CRC byte from the PC, verify, write, ack 'K' -- again strict
; per-record lockstep, same as fsrv.c.
; ---------------------------------------------------------------------
command_put:
	call recv_name
	call reader
	ld l,a
	call reader
	ld h,a
	push hl
	call build_fcb
	ld de,fcb
	ld c,BDOS_DELETE
	call 5
	call build_fcb
	ld de,fcb
	ld c,BDOS_MAKE
	call 5
	cp 0ffh
	jr nz,command_put_made
	pop hl
	call send_error
	ret
command_put_made:
	call set_dma
	ld a,'K'
	call punch
	pop hl
command_put_loop:
	ld a,h
	or l
	jr z,command_put_done
	push hl
	ld hl,dma
	ld b,128
	ld d,0
command_put_recvbyte:
	call reader
	ld (hl),a
	xor d
	ld d,a
	inc hl
	djnz command_put_recvbyte
	call reader
	cp d
	jr z,command_put_crcok
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	ld de,fcb
	ld c,BDOS_DELETE
	call 5
	call send_error
	pop hl
	ret
command_put_crcok:
	ld de,fcb
	ld c,BDOS_WRITE
	call 5
	cp 0
	jr z,command_put_writeok
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	ld de,fcb
	ld c,BDOS_DELETE
	call 5
	call send_error
	pop hl
	ret
command_put_writeok:
	ld a,'K'
	call punch
	pop hl
	dec hl
	jr command_put_loop
command_put_done:
	ld de,fcb
	ld c,BDOS_CLOSE
	call 5
	cp 0ffh
	jr nz,command_put_closeok
	ld de,fcb
	ld c,BDOS_DELETE
	call 5
	call send_error
	ret
command_put_closeok:
	ld a,'K'
	call punch
	ld de,msg_ok
	ld c,BDOS_WRITESTR
	call 5
	ret

; ---------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------
msg_banner:	defb 13,10,"FDD3000 File Server (fsrv, WD2123 CHA)",13,10
		defb "Local Q exits",13,10,"$"
msg_ok:		defb " OK",13,10,"$"
msg_err:	defb " ERR",13,10,"$"
msg_ackerr:	defb " ack err",13,10,"$"
msg_exit:	defb 13,10,"FSRV exit",13,10,"$"

fcb:		defs 36
dma:		defs 128
name_buf:	defs 16
