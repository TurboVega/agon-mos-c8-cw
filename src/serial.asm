;
; Title:	AGON MOS - UART code
; Author:	Dean Belfield
; Created:	11/07/2022
; Last Updated:	29/03/2023
;
; Modinfo:
; 27/07/2022:	Reverted serial_TX back to use RET, not RET.L and increased timeout
; 08/08/2022:	Added check_CTS
; 22/03/2023:	Added serial_PUTCH, moved putch and getch from uart.c
; 23/03/2023:	Renamed serial_RX_WAIT to seral_GETCH
; 29/03/2023:	Added support for UART1
; 20/01/2024:	CW Added support for bidirectional packet protocol

			INCLUDE	"macros.inc"
			INCLUDE	"equs.inc"

			.ASSUME	ADL = 1
			
			DEFINE .STARTUP, SPACE = ROM
			SEGMENT .STARTUP
			
			XDEF	UART0_serial_TX
			XDEF	UART0_serial_RX
			XDEF	UART0_serial_GETCH
			XDEF	UART0_serial_PUTCH
			XDEF	UART0_serial_PUTBUF

			XDEF	_UART0_serial_IDLE
			XDEF	UART1_serial_TX
			XDEF	UART1_serial_RX
			XDEF	UART1_serial_GETCH
			XDEF	UART1_serial_PUTCH
			XDEF	UART0_wait_CTS
			XDEF	_UART0_wait_CTS

			XDEF	_putch
			XDEF	_getch 
			
			XDEF	putch 		
			XDEF	getch 

			XREF	_serialFlags	; In globals.asm
			XREF	_bdpp_driver_flags
			XREF	_bdpp_fg_write_drv_tx_byte_with_usage
			XREF	_bdpp_fg_write_drv_tx_bytes_with_usage
				
UART0_PORT		EQU	%C0		; UART0
UART1_PORT		EQU	%D0		; UART1
				
UART0_REG_RBR:		EQU	UART0_PORT+0	; Receive buffer
UART0_REG_THR:		EQU	UART0_PORT+0	; Transmitter holding
UART0_REG_DLL:		EQU	UART0_PORT+0	; Divisor latch low
UART0_REG_IER:		EQU	UART0_PORT+1	; Interrupt enable
UART0_REG_DLH:		EQU	UART0_PORT+1	; Divisor latch high
UART0_REG_IIR:		EQU	UART0_PORT+2	; Interrupt identification
UART0_REG_FCT:		EQU	UART0_PORT+2;	; Flow control
UART0_REG_LCR:		EQU	UART0_PORT+3	; Line control
UART0_REG_MCR:		EQU	UART0_PORT+4	; Modem control
UART0_REG_LSR:		EQU	UART0_PORT+5	; Line status
UART0_REG_MSR:		EQU	UART0_PORT+6	; Modem status
UART0_REG_SCR:		EQU 	UART0_PORT+7	; Scratch

UART1_REG_RBR:		EQU	UART1_PORT+0	; Receive buffer
UART1_REG_THR:		EQU	UART1_PORT+0	; Transmitter holding
UART1_REG_DLL:		EQU	UART1_PORT+0	; Divisor latch low
UART1_REG_IER:		EQU	UART1_PORT+1	; Interrupt enable
UART1_REG_DLH:		EQU	UART1_PORT+1	; Divisor latch high
UART1_REG_IIR:		EQU	UART1_PORT+2	; Interrupt identification
UART1_REG_FCT:		EQU	UART1_PORT+2;	; Flow control
UART1_REG_LCR:		EQU	UART1_PORT+3	; Line control
UART1_REG_MCR:		EQU	UART1_PORT+4	; Modem control
UART1_REG_LSR:		EQU	UART1_PORT+5	; Line status
UART1_REG_MSR:		EQU	UART1_PORT+6	; Modem status
UART1_REG_SCR:		EQU 	UART1_PORT+7	; Scratch

TX_WAIT			EQU	16384 		; Count before a TX times out

UART_LSR_ERR		EQU 	%80		; Error
UART_LSR_ETX		EQU 	%40		; Transmit empty
UART_LSR_ETH		EQU	%20		; Transmit holding register empty
UART_LSR_RDY		EQU	%01		; Data ready

; Check whether we're clear to send (UART0 only)
;
_UART0_wait_CTS:
UART0_wait_CTS:		GET_GPIO	PD_DR, 8		; Check Port D, bit 3 (CTS)
			JR		NZ, UART0_wait_CTS
			RET

UART1_wait_CTS:		GET_GPIO	PC_DR, 8		; Check Port C, bit 3 (CTS)
			JR		NZ, UART1_wait_CTS
			RET

; Wait for transmitter to be idle
;
_UART0_serial_IDLE:
			PUSH	AF
UART0_serial_IDLE1:	IN0	A,(UART0_REG_LSR)	; Get the line status register
			AND 	UART_LSR_ETX			; Check for TX empty
			JR		Z, UART0_serial_IDLE1	; If clear, then TX is not empty
			POP		AF
			RET 

; Write a character to UART0
; Parameters:
; - A: Data to write
; Returns:
; - F: C if written
; - F: NC if timed out
;
UART0_serial_TX:	PUSH		BC			; Stack BC
			PUSH		AF 			; Stack AF
			LD		BC,TX_WAIT		; Set CB to the transmit timeout
UART0_serial_TX1:	IN0		A,(UART0_REG_LSR)	; Get the line status register
			AND 		UART_LSR_ETH		; Check for TH empty
			JR		NZ, UART0_serial_TX2	; If set, then TH is empty, goto transmit
			DEC		BC
			LD		A, B
			OR		C
			JR		NZ, UART0_serial_TX1
			POP		AF			; We've timed out at this point so
			POP		BC			; Restore the stack
			OR		A			; Clear the carry flag and preserve A
			RET	
UART0_serial_TX2:	POP		AF			; Good to send at this point, so
			OUT0		(UART0_REG_THR),A	; Write the character to the UART transmit buffer
			POP		BC			; Restore BC
			SCF					; Set the carry flag
			RET 

; Write a character to UART1
; Parameters:
; - A: Data to write
; Returns:
; - F: C if written
; - F: NC if timed out
;
UART1_serial_TX:	PUSH		BC			; Stack BC
			PUSH		AF 			; Stack AF
			LD		BC,TX_WAIT		; Set CB to the transmit timeout
UART1_serial_TX1:	IN0		A,(UART1_REG_LSR)	; Get the line status register
			AND 		UART_LSR_ETH		; Check for TH empty
			JR		NZ, UART1_serial_TX2	; If set, then TH is empty, goto transmit
			DEC		BC
			LD		A, B
			OR		C
			JR		NZ, UART1_serial_TX1
			POP		AF			; We've timed out at this point so
			POP		BC			; Restore the stack
			OR		A			; Clear the carry flag and preserve A
			RET	
UART1_serial_TX2:	POP		AF			; Good to send at this point, so
			OUT0		(UART1_REG_THR),A	; Write the character to the UART transmit buffer
			POP		BC			; Restore BC
			SCF					; Set the carry flag
			RET 

 XREF _capture_count
 XREF _capture_data
; Read a character from UART0
; Returns:
; - A: Data read
; - F: C if character read
; - F: NC if no character read
;
UART0_serial_RX:	IN0		A,(UART0_REG_LSR)	; Get the line status register
			AND 		UART_LSR_RDY		; Check for characters in buffer
			RET		Z			; Just ret (with carry clear) if no characters
			IN0		A,(UART0_REG_RBR)	; Read the character from the UART receive buffer

			push bc
			push af
			ld c,a
			ld a,(_capture_count)
			cp a,254
			jr z,isfull
			push hl
			ld hl,_capture_data
			ADD8U_HL
			ld (hl),c
			ld a,(_capture_count)
			inc a
			ld (_capture_count),a
			pop hl
isfull:
			pop af
			pop bc

			SCF 					; Set the carry flag
			RET

; Read a character from UART1
; Returns:
; - A: Data read
; - F: C if character read
; - F: NC if no character read
;
UART1_serial_RX:	IN0		A,(UART1_REG_LSR)	; Get the line status register
			AND 		UART_LSR_RDY		; Check for characters in buffer
			RET		Z			; Just ret (with carry clear) if no characters
			IN0		A,(UART1_REG_RBR)	; Read the character from the UART receive buffer
			SCF 					; Set the carry flag
			RET

; Read a character from UART0 (blocking)
; Returns:
; - A: Data read
; - F: C if read
; - F: NC if UART not enabled
;
UART0_serial_GETCH:	PUSH		AF 
			LD		A, (_serialFlags)
			TST		01h
			JR		Z, UART_serial_NE
			POP		AF
$$:			CALL 		UART0_serial_RX
			JR		NC,$B
			RET 

; Read a character from UART1 (blocking)
; Returns:
; - A: Data read
; - F: C if read
; - F: NC if UART not enabled
;
UART1_serial_GETCH:	PUSH		AF 
			LD		A, (_serialFlags)
			TST		01h
			JR		Z, UART_serial_NE
			POP		AF
$$:			CALL 		UART1_serial_RX
			JR		NC,$B
			RET 

; Write a character to UART0 (blocking),
; or to a BDDP packet (possibly blocking)
;
; Parameters:
; - A: Character to write out
; Returns:
; - F: C if written
; - F: NC if UART not enabled
;
UART0_serial_PUTCH:
			PUSH BC
			LD C, A				; Save character in C (lower 8 bits of parameter)
			LD	A, (_bdpp_driver_flags)	; Get the BDPP driver flags
			AND	A, 03h					; Check for BDPP_FLAG_ALLOWED + BDPP_FLAG_ENABLED
			CP	A, 03h					; Are we in packet mode?
			JR  NZ, UART0_serial_PUTCH_1 ; Go if not (use direct mode)

			PUSH HL
			PUSH IX
			PUSH IY
			PUSH DE

			LD B, 0						; Clear upper middle 8 bits of parameter
			PUSH BC						; Set 24-bit parameter for C call
			CALL _bdpp_fg_write_drv_tx_byte_with_usage ; Give the data byte to BDPP
			POP BC						; Unstack the parameter

			POP DE
			POP IY
			POP IX
			POP HL
			POP BC
			SCF							; Indicate character written
			RET
			
UART0_serial_PUTCH_1:
			LD	A, (_serialFlags)		; Get the serial flags
			TST	01h						; Check UART is enabled
			JR	Z, UART_serial_NE		; If not, then skip
			TST	02h						; If hardware flow control enabled then
			CALL	NZ, UART0_wait_CTS	; Wait for clear to send signal
			LD	A, C					; Restore character to A
$$:			CALL	UART0_serial_TX		; Send the character
			JR	NC, $B					; Repeat until sent
			POP BC
			RET

; Write multiple characters to UART0 (blocking),
; or to a BDDP packet (possibly blocking)
;
; Parameters:
; - HLU: Pointer to characters to write out
; - BC:  Number of characters to write out
; Returns:
; - F: C if written
; - F: NC if UART not enabled
;
UART0_serial_PUTBUF:
			LD	A, (_serialFlags)		; Get the serial flags
			TST	01h						; Check UART is enabled
			JR	Z, UART_serial_NE		; If not, then skip

			TST	02h						; If hardware flow control enabled then
			CALL NZ, UART0_wait_CTS		; Wait for clear to send signal

			LD	A, (_bdpp_driver_flags)	; Get the BDPP driver flags
			AND	A, 03h					; Check for BDPP_FLAG_ALLOWED + BDPP_FLAG_ENABLED
			CP	A, 03h					; Are we in packet mode?
			JR  NZ, UART0_serial_PUTBUF_2 ; Go if not (use direct mode)

			PUSH IX
			PUSH IY
			PUSH DE
			PUSH BC						; Set 24-bit parameter for C call
			PUSH HL
			CALL _bdpp_fg_write_drv_tx_bytes_with_usage ; Give the data bytes to BDPP
			POP HL
			POP BC						; Unstack the parameter
			POP DE
			POP IY
			POP IX
			SCF							; Indicate characters written
			RET
			
UART0_serial_PUTBUF_2:
			LD	A, 	(HL)				; Get a character
$$:			CALL	UART0_serial_TX		; Send the character
			JR	NC, $B					; Repeat until sent
			INC 	HL 					; Increment the buffer pointer
			DEC	BC						; Reduce byte count
			LD	A, B					; Part of BC
			OR	A, C					; Other part of BC
			JR	NZ, UART0_serial_PUTBUF_2 ; Go back if more to send
			RET

; Write a character to UART1 (blocking)
; Parameters:
; - A: Character to write out
; Returns:
; - F: C if written
; - F: NC if UART not enabled
;
UART1_serial_PUTCH:	PUSH	AF
			LD	A, (_serialFlags)		; Get the serial flags
			TST	10h				; Check UART is enabled
			JR	Z, UART_serial_NE		; If not, then skip (reuses UART0 routine here)
			TST	20h				; If hardware flow control enabled then
			CALL	NZ, UART1_wait_CTS		; Wait for clear to send signal
			POP	AF			
$$:			CALL	UART1_serial_TX			; Send the character
			JR	NC, $B				; Repeat until sent
			RET

; Called by UART0 and UART1 PUTCH and GETCH if the UART is not enabled
;
UART_serial_NE:		POP	AF				; Tidy up the stack
			OR	A 				; Clear the carry flag
			RET
;
; The C wrappers
;

; INT putch(INT ch);
;
; Write a character out to the UART
; Parameters:
; - ch: The character to write (least significant byte)
; Returns:
; - The character written
;
_putch:
putch:			PUSH	IY				; Standard C prologue
			LD	IY, 0
			ADD	IY, SP	

			LD	A, (IY+6)			; INT ch (least significant byte)
			LD	HL, 0				; HLU: The return value
			LD	L, A 
			CALL	UART0_serial_PUTCH		; Output the character

			LD 	SP, IY				; Standard epilogue
			POP	IY
			RET

; INT getch(VOID);
;
; Read a character out to the UART - waits for character input
; Returns:
; - The character read
;
_getch:
getch:			PUSH	IY				; Standard C prologue
			LD	IY, 0
			ADD	IY, SP	

			CALL	UART0_serial_GETCH		; Read the character
			LD	HL, 0				; HLU: The return value
			LD	L, A

			LD 	SP, IY				; Standard epilogue
			POP	IY
			RET