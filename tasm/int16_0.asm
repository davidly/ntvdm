; This function implements string input for int 0x16 ah 0 -- character input
; On output, al contains the ascii character
;            ah contains the scancode
; this function blocks until a character is available
;
; build like this:
;     ntvdm -c tasm int16_0.asm
;     ntvdm -c tlink int16_0.obj
;     hd int16_0.exe /q /o:512 /n
;     then copy/paste the hex output to int16_0_code[] in ntvdm.cxx


.model large

code segment
assume cs:code

kbd_head equ 1ah
kbd_tail equ 1ch
kbd_start equ 1eh
kbd_beyond equ 3eh
bios_seg equ 40h

begin:
    push bx
    push es

    mov ax, bios_seg
    mov es, ax

  wait_for_kbd:                                  ; wait for head != tail
    mov ax, es: [kbd_head]
    cmp ax, es: [kbd_tail]
    je wait_for_kbd

    mov bx, word ptr es: [kbd_head]
    mov al, byte ptr es: [bx]			 ; store the ascii character
    inc word ptr es: [kbd_head]                  ; consume the ascii char
    inc bx
    mov ah, byte ptr es: [bx]			 ; store the scancode
    inc word ptr es: [kbd_head]                  ; consume the scancode (thrown away)

    cmp word ptr es: [kbd_head], kbd_beyond      ; has the head moved beyond the buffer?
    jl all_done
    mov word ptr es: [kbd_head], kbd_start       ; if so, restore it to the start

  all_done:
    pop es
    pop bx
    retf

code ends               
end

