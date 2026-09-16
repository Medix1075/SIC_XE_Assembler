TEST    START   0
        LDA     #5
        USE     CODE
LOOP    +JSUB   SUBR
        JLT     ENDL
        USE     DEFAULT
BUF     RESB    16
        USE     CODE
SUBR    CLEAR   X
        LDA     =W'BUF'     ; literal referencing symbol (relocation shown)
        LTORG
        RSUB
ENDL    LDA     =C'Z'
        +STA    OUT
OUT     RESW    1
        END     TEST
