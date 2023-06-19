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
bios_seg equ 40h

begin:
    mov ah, 0
    int 16h
    mov ah, 8                                    ; restore the function to ah, which 16/0 overwrote with the scancode
    retf

code ends               
end

