rem errorlevel can't be used because all 3 apps use the value-less cp/m exit function

del %1.exe

ntvdm mc1 %1

ntvdm mc2 %1

ntvdm link %1 + cs,, %1.map, mcs

ntvdm -p %1.exe

