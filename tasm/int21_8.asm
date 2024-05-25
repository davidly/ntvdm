; This function implements string input for int 0x21 ah 0x8 -- character input
; On output, al contains the ascii character
; this function blocks until a character is available
;
; build like this:
;     ntvdm -c tasm int21_8.asm
;     ntvdm -c tlink int21_8.obj
;     hd int21_8.exe /q /o:512 /n
;     then copy/paste the hex output to int21_8_code[] in ntvdm.cxx


.model large

code segment
assume cs:code

kbd_head equ 1ah
kbd_tail equ 1ch
kbd_start equ 1eh
kbd_beyond equ 3eh
kbd_midway equ 0d0h  ; 1 if ascii consumed already and scancode is next. non-DOS-standard location
bios_seg equ 40h

begin:
    push es
    push bx
    mov ax, bios_seg
    mov es, ax

  wait_for_kbd:                                  ; wait for a keystroke to be available
    mov ah, 1                                    ; use int16 1 instead of a busy loop to enable the emulator to not pin the CPU
    injectcode db 0cdh, 69h, 16h                 ; syscall int 16. don't use int 16 because apps like qc 2.0 hook that
    ; int 16
    jz wait_for_kbd

    cli                                          ; updating global kbd state, so don't allow interrupts
    mov bx, word ptr es: [kbd_head]
    cmp byte ptr es: [kbd_midway], 0             ; check if the ascii has been consumed yet
    jz return_ascii

    mov al, byte ptr es: [bx + 1]                ; return the scancode
    mov byte ptr es: [kbd_midway], 0             ; clear the midway flag
    jmp consume_keystroke

  return_ascii:
    mov al, byte ptr es: [bx]
    cmp al, 0
    jnz consume_keystroke                        ; don't consume if ascii is 0

    mov byte ptr es: [kbd_midway], 1             ; remember to return the scancode on the next call
    jmp all_done                                 ; don't consume the keystroke yet

  consume_keystroke:
    add word ptr es: [kbd_head], 2               ; move past the scancode if there was one
    cmp word ptr es: [kbd_head], kbd_beyond      ; has the head moved beyond the buffer?
    jl all_done
    mov word ptr es: [kbd_head], kbd_start       ; if so, restore it to the start

  all_done:
    sti
    mov ah, 8                                    ; restore the function to ah since it was destroyed
    pop bx
    pop es
    retf

code ends               
end

