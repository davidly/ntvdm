; This function implements string input for int 0x21 ah 0xa -- buffered input
; ds:dx points to the buffer
; byte 0 count of bytes in
; byte 1 count of bytes out excluding CR
; byte 2..n string ending in CR (unless it doesn't fit)
;
; if the input buffer is full, return immediately. don't wait for a CR.
;
; build like this:
;     ntvdm -c tasm int21_a.asm
;     ntvdm -c tlink int21_a.obj
;     hd int21_a.exe /q /o:512 /n
;     then copy/paste the hex output to int21_a_code[] in ntvdm.cxx


.model large

code segment
assume cs:code

LF equ 0ah
CR equ 0dh
BS equ 08h

begin:
    push bx
    push si

    mov si, dx                        ; si has the caller's buffer
    mov byte ptr ds: [si + 1], 0      ; # of characters written to caller's buffer

    cmp byte ptr ds: [si + 0], 0      ; see if caller asked for 0 characters
    jz all_done

  get_another_char:
    mov ah, 1                         ; poll until a character is available
    int 16h
    jz get_another_char

    mov ah, 0                         ; get the character
    int 16h
    cmp al, BS                        ; is it a backspace?
    jnz not_backspace
    cmp byte ptr ds: [si + 1], 0      ; don't backspace if at start of the buffer
    jz get_another_char
    dec byte ptr ds: [si + 1]         ; remove the last character
    jmp display_char

  not_backspace:
    xor bx, bx
    mov bl, byte ptr ds: [si + 1 ]    ; save the character in the buffer
    mov byte ptr ds: [si + bx + 2], al
    cmp al, CR
    je all_done                       ; if it's a CR, we're done
    inc byte ptr ds: [si + 1]         ; increment after CR check, since it's not included in the count

  display_char:                       ; write the character to the screen
    mov dl, al
    mov ah, 2
    int 21h

    mov al, byte ptr ds: [si + 1]     ; check if the buffer is full
    cmp al, byte ptr ds: [si + 0]
    jnz get_another_char              ; if the input buffer is full, just return what we have. no CR appended

  all_done:
    clc                               ; clear carry to indicate no error
    pop si
    pop bx
    retf

code ends               
end

