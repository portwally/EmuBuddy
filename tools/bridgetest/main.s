*-------------------------------------------------------------
* BridgeTest: exercises the EmuBuddy host bridge (WDM $EB,$00).
* It makes the folder "fromiigs" in the bridge folder, writes two files
* ("hello" as text, "GAME" as type $B3 aux $DB03) and a "status" file with
* one 16-bit result per call (0 = ok, $FFFF = no bridge answered).
* On a real IIgs WDM does nothing, so status stays $FFFF and nothing is
* written.
*-------------------------------------------------------------
            rel
            mx    %00

* --- one bridge call: ]1 = command ---
DoCmd       MAC
            lda   #]1
            sta   Block
            lda   #$FFFF
            sta   Block+2
            lda   #Block
            dfb   $42,$EB,$00      ; WDM $EB,$00 : DB:A = the block
            ldx   StatIx
            lda   Block+2
            sta   Stats,x
            inx
            inx
            stx   StatIx
            <<<

* --- Block+]2 = a pointer to ]1 (24-bit, in this program's bank) ---
SetPtr      MAC
            lda   #]1
            sta   Block+]2
            phk
            pla
            and   #$00FF
            sta   Block+]2+2
            <<<

* --- Block+]2 = the 16-bit value ]1 (and a zero high word) ---
SetVal      MAC
            lda   #]1
            sta   Block+]2
            stz   Block+]2+2
            <<<

Start       phk
            plb
            rep   #$30
            stz   StatIx
            DoCmd $0000            ; INFO
            lda   Stats
            cmp   #$FFFF
            bne   :bridge
            jmp   Quit             ; no bridge: nothing to do
:bridge     SetPtr PathDir;$04
            DoCmd $0011            ; MKDIR fromiigs
            SetPtr PathHello;$04
            SetVal 2;$08           ; mode: write
            SetVal 4;$0C           ; type TXT
            SetVal 0;$10           ; aux 0
            DoCmd $0020            ; OPEN hello
            lda   Block+$14
            sta   Handle
            sta   Block+$04
            stz   Block+$06
            SetPtr HelloData;$08
            SetVal HelloLen;$0C
            DoCmd $0022            ; WRITE
            lda   Handle
            sta   Block+$04
            stz   Block+$06
            DoCmd $0023            ; CLOSE
            SetPtr PathGame;$04
            SetVal 2;$08
            SetVal $B3;$0C         ; type S16
            SetVal $DB03;$10       ; aux
            DoCmd $0020            ; OPEN GAME
            lda   Block+$14
            sta   Handle
            sta   Block+$04
            stz   Block+$06
            SetPtr GameData;$08
            SetVal 4;$0C
            DoCmd $0022            ; WRITE
            lda   Handle
            sta   Block+$04
            stz   Block+$06
            DoCmd $0023            ; CLOSE
            SetPtr PathStatus;$04
            SetVal 2;$08
            SetVal 6;$0C           ; type BIN
            SetVal 0;$10
            DoCmd $0020            ; OPEN status
            lda   Block+$14
            sta   Handle
            sta   Block+$04
            stz   Block+$06
            SetPtr Stats;$08
            lda   StatIx
            sta   Block+$0C
            stz   Block+$0E
            DoCmd $0022            ; WRITE (the statuses so far)
            lda   Handle
            sta   Block+$04
            stz   Block+$06
            DoCmd $0023            ; CLOSE

Quit        jsl   $E100A8          ; GS/OS call
            dw    $2029            ; Quit
            adrl  QuitParms
:hang       bra   :hang

QuitParms   dw    2
            adrl  0
            dw    0

StatIx      dw    0
Handle      dw    0
Stats       ds    32

Block       dw    0                ; command
            dw    0                ; status
            adrl  0,0,0,0          ; p1..p4
            adrl  0,0,0,0          ; r1..r4

PathDir     dw    8
            asc   'fromiigs'
PathHello   dw    14
            asc   'fromiigs/hello'
PathGame    dw    13
            asc   'fromiigs/GAME'
PathStatus  dw    15
            asc   'fromiigs/status'
HelloData   asc   'Hello from the IIgs'
            dfb   $0D
HelloLen    =     20
GameData    dfb   $01,$02,$03,$04
