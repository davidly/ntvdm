; This function implements string input for int 0x21 ah 0x1 -- character input with echo
; On output, al contains the ascii character
; this function blocks until a character is available
;
; build like this:
;     ntvdm -c tasm int21_1.asm
;     ntvdm -c tlink int21_1.obj
;     hd int21_1.exe /q /o:512 /n
;     then copy/paste the hex output to int21_1_code[] in ntvdm.cxx


.model large

code segment
assume cs:code

begin:
    mov ah, 0
    injectcodeA db 69h, 16h                      ; fint 16. don't use int 16 because apps like qc 2.0 hook that
    ; int 16h

    mov ah, 0ah                                  ; echo the character in al
    injectcodeB db 69h, 10h                      ; fint 10.
    ; int 10h

    mov ah, 1                                    ; restore the function to ah
    retf

code ends               
end

