*-------------------------------------------------------------
* Receive: copies the files EmuBuddy staged for this IIgs into the GS/OS
* folders they were dropped on. (The helper in tools/helper does the same by
* itself; this program is for running it by hand.)
*-------------------------------------------------------------
            rel
            mx    %00

* the program starts at the first byte: jump over the engine
Entry       jmp   Start

            put   engine.s

UserID      dw    0

Start       phk
            plb
            rep   #$30
            pha
            ldx   #$0202           ; MMStartUp
            jsl   $E10000
            pla
            sta   UserID
* the copy buffer: fixed, locked, not crossing a bank
            pha
            pha
            pea   $0000
            pea   $4000            ; 16 KB
            lda   UserID
            pha
            pea   $C010
            pea   $0000
            pea   $0000
            ldx   #$0902           ; NewHandle
            jsl   $E10000
            pla
            sta   Hand
            pla
            sta   Hand+2
            bcc   :got
            jmp   Quit
:got        ldy   #0
            lda   [Hand],y
            sta   BufPtr
            ldy   #2
            lda   [Hand],y
            sta   BufPtr+2
            jsr   ProcessInbox
            lda   Hand+2           ; free the copy buffer
            pha
            lda   Hand
            pha
            ldx   #$1002           ; DisposeHandle
            jsl   $E10000
Quit        GSOS  $2029;QuitParms
:hang       bra   :hang
