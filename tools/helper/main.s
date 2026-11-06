*-------------------------------------------------------------
* EmuBuddy Helper: a permanent init (PIF, type $B6) for SYSTEM/SYSTEM.SETUP.
* GS/OS runs it at boot. It installs a heartbeat task; once a second the
* task asks the Scheduler to run the copy engine (tools/receive/engine.s) when
* GS/OS is not busy. The engine copies the files EmuBuddy staged in the bridge
* folder into the GS/OS folders they were dropped on, so nothing has to be
* started by hand. On a real IIgs (no bridge) it finds nothing and ends at once.
*-------------------------------------------------------------
            rel
            mx    %00

Entry       jmp   Start

            put   engine.s

* ---- the heartbeat task: a header, then code (called with JSL) ----
HbBlock     adrl  0                ; link (the system uses it)
HbCount     dw    60               ; heartbeats until it runs (60 = a second)
            dw    $A55A            ; signature
HbCode      phb
            phk
            plb
            php
            rep   #$30
            pha
            phx
            phy
            lda   #60
            sta   HbCount          ; run again in a second
            lda   Pending
            bne   :busy
            lda   #1
            sta   Pending
            pha                    ; result space
            lda   SchPtr+2
            pha
            lda   SchPtr
            pha
            ldx   #$0907           ; SchAddTask
            jsl   $E10000
            pla
            bne   :busy            ; (the Scheduler accepted it)
            stz   Pending          ; queue full: try again next time
:busy       rep   #$30
            ply
            plx
            pla
            plp
            plb
            rtl

* ---- the task the Scheduler runs when GS/OS is not busy ----
Deferred    phb
            phk
            plb
            phd
            php
            rep   #$30
            pha
            phx
            phy
            lda   DPAddr
            tcd                    ; the engine's direct page (bank 0)
            jsr   ProcessInbox
            rep   #$30
            stz   Pending
            ply
            plx
            pla
            plp
            pld
            plb
            rtl

* ---- the init: runs once at boot ----
Start       phb
            phk
            plb
            phd
            php
            rep   #$30
            sta   InitID           ; GS/OS gives an init its user ID in A
            pha
            phx
            phy
* a bank 0 direct page of 256 bytes for the engine
            pha
            pha
            pea   $0000
            pea   $0100
            lda   InitID
            pha
            pea   $C005            ; locked, fixed, bank 0, page aligned
            pea   $0000
            pea   $0000
            ldx   #$0902           ; NewHandle
            jsl   $E10000
            pla
            sta   HLo
            pla
            sta   HHi
            bcs   :fail
            jsr   Deref
            lda   HLo
            sta   DPAddr
* the 16 KB copy buffer (not crossing a bank)
            pha
            pha
            pea   $0000
            pea   $4000
            lda   InitID
            pha
            pea   $C010
            pea   $0000
            pea   $0000
            ldx   #$0902           ; NewHandle
            jsl   $E10000
            pla
            sta   HLo
            pla
            sta   HHi
            bcs   :fail
            jsr   Deref
            lda   HLo
            sta   BufPtr
            lda   HHi
            sta   BufPtr+2
* the heartbeat
            lda   #Deferred
            sta   SchPtr
            sep   #$20
            phk
            pla
            sta   SchPtr+2
            stz   SchPtr+3
            rep   #$20
            lda   #HbBlock
            sta   TaskPtr
            sep   #$20
            phk
            pla
            sta   TaskPtr+2
            stz   TaskPtr+3
            rep   #$20
            lda   TaskPtr+2
            pha
            lda   TaskPtr
            pha
            ldx   #$1203           ; SetHeartBeat
            jsl   $E10000
:fail       rep   #$30
            ply
            plx
            pla
            plp
            pld
            plb
            rtl

* HLo/HHi = a handle; afterwards the address of the block (read through a
* direct page made on the stack, so nobody else's direct page is touched)
Deref       lda   HHi
            pha
            lda   HLo
            pha
            phd
            tsc
            clc
            adc   #3               ; D+0 = the handle (low word, bank)
            tcd
            ldy   #0
            lda   [0],y
            tax
            ldy   #2
            lda   [0],y
            pld
            sta   HHi
            stx   HLo
            pla
            pla
            rts

InitID      dw    0
HLo         dw    0
HHi         dw    0
DPAddr      dw    0
TaskPtr     adrl  0
SchPtr      adrl  0
Pending     dw    0
