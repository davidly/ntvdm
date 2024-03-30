; This function implements string input for int 0x21 ah 0x3f when bx is 0 -- read from console/stdin
; ds:dx points to the buffer
; cx = # bytes in buffer on input
; ax = # of bytes read on output
; cf = cleared on success, set on error
; strings must end with the characters CR LF unless the input buffer length limit is reached
; if there is space in the temporary buffer, nothing is returned until CR is pressed.
; characters not implemented: INS, DEL, TAB, left and right arrow keys
; some apps (GW-BASIC in terminal mode) call this to read 1 character at a time but expect backspace processing. too bad for them.
; build like this:
;     ntvdm -c tasm int21_3f.asm
;     ntvdm -c tlink int21_3f.obj
;     hd int21_3f.exe /q /o:512 /n
;     then copy/paste the hex output to int21_3f_code[] in ntvdm.cxx

.model large

code segment
assume cs:code
buffer_size equ 80
LF equ 0ah
CR equ 0dh
BS equ 08h

begin:
    push bx
    push cx
    push dx
    push di
    push si

    mov di, offset buffer             ; di has the local buffer
    mov si, dx                        ; si has the caller's buffer
    mov cs: [char_count], 0           ; # of characters written to caller's buffer

    cmp cx, 0                         ; see if caller asked for 0 characters
    jnz limit_request_size            ; load_count_exit is too far away for jz
    jmp load_count_exit

  limit_request_size:
    cmp cx, buffer_size               ; if asking for > the buffer size, reduce the ask
    jle update_output_len
    mov cx, buffer_size

  update_output_len:
    mov cs: [output_len], cx
    mov ax, cs: [buffer_current]      ; check if buffer has any characters we can return now
    cmp ax, cs: [buffer_used]
    jl copy_buffer                    ; if so, no need to read the keyboard; just copy what we have

  get_another_char:
    mov ah, 1                         ; poll until a character is available
    int 16h
    jz get_another_char

    mov ah, 0                         ; get the character
    int 16h
    cmp cs: [output_len], 1           ; if 1 character is requested, save even BS
    jz save_character
    cmp al, BS                        ; is it a backspace?
    jnz save_character
    cmp cs: [buffer_used], 0          ; don't backspace if at start of the buffer
    jz get_another_char
    dec cs: [buffer_used]             ; remove the last character
    jmp display_char

  save_character:
    mov bx, cs:[buffer_used]          ; save the character in the buffer
    mov byte ptr cs: [di + bx], al
    inc cs: [buffer_used]
    cmp al, CR
    je append_lf                      ; if it's a CR, we're done getting new chars

    cmp al, BS                        ; if it's not BS, display it
    jnz display_char
    cmp cs: [output_len], 1           ; if it's 1 char only, don't show BS else show it
    je check_if_full

  display_char:                       ; write the character to the screen
    mov dl, al
    mov ah, 2
    int 21h

  check_if_full:
    mov ax, cs: [buffer_used]         ; check if the buffer is full
    cmp ax, cs: [output_len]
    jz copy_buffer                    ; if the input buffer is full, just return what we have
    cmp ax, buffer_size-1             ; -1 to save space for a LF at the end
    jnz get_another_char

  append_lf:
    mov bx, cs: [buffer_used]         ; put a LF at the end
    mov byte ptr cs: [di + bx], LF
    inc cs: [buffer_used]
    mov dl, LF                        ; write the LF to the screen
    mov ah, 2
    int 21h

  copy_buffer:                        ; copy character(s) to the caller's buffer
    mov bx, cs: [buffer_current]
    mov ax, cs: [di + bx]
    mov bx, cs: [char_count]
    mov ds: [si + bx], ax
    inc cs: [char_count]
    inc cs: [buffer_current]

  reset_update_buffer_info:
    mov ax, cs: [buffer_current]      ; if the buffer is consumed, reset it
    cmp ax, cs: [buffer_used]
    jne buffer_remains
    mov cs: [buffer_current], 0       ; nothing left in the buffer
    mov cs: [buffer_used], 0
    jmp load_count_exit

  buffer_remains:                     ; check if the output buffer is full
    mov ax, cs: [output_len]
    cmp ax, cs: [char_count]
    jne copy_buffer                   ; if more space, loop to copy another character

  load_count_exit:
    mov ax, cs: [char_count]          ; load the # of characters in the buffer
    clc                               ; clear carry to indicate no error
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    retf

output_len dw 0                       ; size of the buffer from the caller
char_count dw 0                       ; # of characters written to the caller's buffer
buffer_current dw 0                   ; index of the next character in buffer to return
buffer_used dw 0                      ; # of bytes used in buffer
buffer db buffer_size dup (0)         ; buffer of characters read until a CR

code ends               
end

