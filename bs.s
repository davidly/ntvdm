  bits 16
  cpu 8086

; Boot sector app to write a message to the screen, wait for a keystrong, and halt.
; The code assumes it'll be loaded at 07c0:0000 with cs:ip set to those values.
; Boot Sectors are typically loaded by the BIOS

entry:
    mov ax, cs                  ; not sure if ds is setup at this point, but cs should be
    mov ds, ax

    mov ax, 0x0003              ; set video mode to 80x25 16 color CGA/EGA/VGA
    int 10h

    mov ax, 0x0500              ; set active display page to 0
    int 10h

    mov ax, 0x0200              ; set cursor position
    mov bh, 0                   ; page 0
    mov dh, 3                   ; row
    mov dl, 5                   ; col
    int 10h

    mov di, message             ; di points to the current char in the message

  _next_char:
    mov ah, 0x0e                ; write character in teletype mode command
    mov al, [di]                ; load the character
    cmp al, 0                   ; check for end of string
    jz _message_done
    mov bh, 0                   ; video page 0
    mov cx, 1                   ; write 1 character
    push di                     ; save di in case the interrupt destroys it
    int 10h                     ; write the character
    pop di
    inc di                      ; move to next char
    jmp _next_char

  _message_done:
    mov ax, 0                   ; wait for a keystroke
    int 16h

    hlt                         ; and catch fire

message:
  db 'hello from the boot sector, press a key to exit', 0dh, 0ah, 0

; make the file 512 bytes long -- one sector. The aa55 signature is what DOS uses

  times 510-($-$$) db 0
  db 0x55, 0xaa
