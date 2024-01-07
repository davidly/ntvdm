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
    push es

    mov ax, bios_seg
    mov es, ax

  wait_for_kbd:                                  ; wait for a keystroke to be available
    mov ah, 1                                    ; use int16 1 instead of a busy loop to enable the emulator to not pin the CPU
    injectcode db 69h, 16h                       ; fint 16. don't use int 16 because apps like qc 2.0 hook that
    ; int 16
    jz wait_for_kbd

    cli
    add word ptr es: [kbd_head], 2               ; consume the character and scancode
    cmp word ptr es: [kbd_head], kbd_beyond      ; has the head moved beyond the buffer?
    jl all_done
    mov word ptr es: [kbd_head], kbd_start       ; if so, restore it to the start

  all_done:
    sti
    pop es
    retf

code ends               
end

