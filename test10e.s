  bits 16
  cpu 8086

; tests bios int 10h AH = 0eh -- write text in teletype mode
; al == ascii character, bh = video page number, bl = foreground pixel color (graphics mode only)
; advanced cursor. BEL(7), BS(8), LF(ah), and CR(dh) are honored

org 100h
entry:
    mov ax, 0x0003              ; set video mode to 80x25 16 color CGA/EGA/VGA
    int 10h

    mov ax, 0x0500              ; set active display page to 0
    int 10h

    mov ax, 0x0200              ; set cursor position to 10, 10
    mov bh, 0                   ; page 0
    mov dh, 3                   ; row
    mov dl, 5                   ; col
    int 10h

    mov al, 'a'
    mov ah, 0eh
    int 10h

    mov al, 'b'
    mov ah, 0eh
    int 10h

    mov al, 'c'
    mov ah, 0eh
    int 10h

    mov al, 'd'
    mov ah, 0eh
    int 10h

    mov al, 08h                 ; backspace
    mov ah, 0eh
    int 10h

    mov ax, 0                   ; wait for a keystroke
    int 16h

    mov al, 0dh
    mov ah, 0eh
    int 10h

    mov al, 0ah
    mov ah, 0eh
    int 10h

    mov al, 'e'
    mov ah, 0eh
    int 10h

    mov al, 'f'
    mov ah, 0eh
    int 10h

    mov ax, 0                   ; wait for a keystroke
    int 16h

    mov al, 0ah
    mov ah, 0eh
    int 10h

    ret                         ; cp/m compatible exit

