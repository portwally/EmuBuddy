*-------------------------------------------------------------
* Receive: copies the files EmuBuddy staged for this IIgs into the GS/OS
* folders they were dropped on (docs/BRIDGE.md, "Dropping files").
*
*   Inbox/NNNN/<name>      the file (type and aux type in the host name)
*   Inbox/NNNN/target.txt  the GS/OS folder, e.g. :Boot.Disk:Dragtests
*   Outbox/NNNN.txt        the note this program leaves: "OK path" or
*                          "ERR $xx path"
* A folder without target.txt is still being written: it is skipped.
* Everything is reached through the EmuBuddy host bridge: WDM $EB,$00.
*-------------------------------------------------------------
            rel
            mx    %00

* ---- direct page variables ----
Src         equ   $00              ; pointers into this program's bank
Dst         equ   $02
Cnt         equ   $04
Base        equ   $06
Hand        equ   $20              ; the memory handle (long)
EntPtr      equ   $30              ; current entry of the Inbox list
EntLeft     equ   $32
NLen        equ   $34              ; name of the drop folder
NPtr        equ   $36
Ent2Ptr     equ   $38              ; current entry of the folder list
Ent2Left    equ   $3A
DLen        equ   $3C              ; the data file's name
DPtr        equ   $3E
DType       equ   $40
DAux        equ   $42
HaveTarget  equ   $44
RefDst      equ   $46              ; GS/OS file reference
HandIn      equ   $48              ; bridge file reference
ErrCode     equ   $4A
Tmp         equ   $4C
Count       equ   $4E

* the program starts at the first byte: jump over the subroutines
Entry       jmp   Start

* ---- one bridge call: A = command, returns the status in A ----
BCall       sta   Block
            lda   #$FFFF
            sta   Block+2
            lda   #Block
            dfb   $42,$EB,$00      ; WDM $EB,$00 : DB:A = the block
            lda   Block+2
            rts

* ---- Block+]2 = the 16-bit value ]1 ----
SetVal      MAC
            lda   #]1
            sta   Block+]2
            stz   Block+]2+2
            <<<

* ---- Block+]2 = a pointer to ]1 in this bank ----
SetPtr      MAC
            lda   #]1
            sta   Block+]2
            sep   #$20
            phk                    ; (8-bit: push 1 byte, pull 1 byte)
            pla
            sta   Block+]2+2
            stz   Block+]2+3
            rep   #$20
            <<<

* ---- Block+]1 = the big buffer ----
SetBuf      MAC
            lda   BufPtr
            sta   Block+]1
            lda   BufPtr+2
            sta   Block+]1+2
            <<<

* ---- a GS/OS call ]1 with the parameter block ]2 ----
GSOS        MAC
            jsl   $E100A8
            dw    ]1
            adrl  ]2
            <<<

* ---- the long pointer at ]1+]2 = address of label ]3 ----
LongPtr     MAC
            lda   #]3
            sta   ]1+]2
            sep   #$20
            phk
            pla
            sta   ]1+]2+2
            stz   ]1+]2+3
            rep   #$20
            <<<

* ---- append ]3 bytes from ]2 to the string buffer ]1 (length word first) ----
Append      MAC
            ldx   #]1
            ldy   #]2
            lda   #]3
            jsr   AppendMem
            <<<

* X = buffer, Y = source address, A = count
AppendMem   stx   Base
            sty   Src
            sta   Cnt
            lda   (Base)
            clc
            adc   Base
            adc   #2
            sta   Dst
            sep   #$20
            ldy   #0
:lp         cpy   Cnt
            beq   :done
            lda   (Src),y
            sta   (Dst),y
            iny
            bra   :lp
:done       rep   #$30
            lda   (Base)
            clc
            adc   Cnt
            sta   (Base)
            rts

* A = a number; HexTmp = its two hex digits
ToHex       pha
            lsr
            lsr
            lsr
            lsr
            and   #$000F
            tax
            sep   #$20
            lda   HexDigits,x
            sta   HexTmp
            rep   #$20
            pla
            and   #$000F
            tax
            sep   #$20
            lda   HexDigits,x
            sta   HexTmp+1
            rep   #$20
            rts

*-------------------------------------------------------------
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

* is there a bridge, with a folder?
            SetVal 0;$04
            SetVal 0;$08
            lda   #$0000           ; INFO
            jsr   BCall
            beq   :bridge
            jmp   Done
:bridge     SetPtr PathOutbox;$04
            lda   #$0011           ; MKDIR Outbox (it may exist)
            jsr   BCall
            SetPtr PathInbox;$04
            SetPtr ListBuf;$08
            SetVal 4096;$0C
            SetVal 0;$10
            lda   #$0010           ; LIST Inbox
            jsr   BCall
            beq   :listed
            jmp   Done             ; no Inbox: nothing to receive
:listed     lda   Block+$14
            sta   EntLeft
            lda   #ListBuf
            sta   EntPtr

NextEnt     lda   EntLeft
            bne   :go
            jmp   Done
:go         lda   EntPtr
            sta   Src
            ldy   #2
            lda   (Src),y          ; flags, type
            and   #$00FF
            sta   Tmp
            ldy   #10
            lda   (Src),y
            sta   NLen
            lda   EntPtr
            clc
            adc   #12
            sta   NPtr
            ldy   #0
            lda   (Src),y          ; entry length
            clc
            adc   EntPtr
            pha                    ; the next entry
            dec   EntLeft
            lda   Tmp
            and   #$0001
            beq   :skip            ; a file: not a drop folder
            jsr   DoDrop
:skip       pla
            sta   EntPtr
            jmp   NextEnt

Done        lda   Hand+2           ; free the copy buffer (not DisposeAll: the
            pha                    ; program's own memory has the same user ID)
            lda   Hand
            pha
            ldx   #$1002           ; DisposeHandle
            jsl   $E10000
Quit        GSOS  $2029;QuitParms
:hang       bra   :hang

*-------------------------------------------------------------
* One drop folder Inbox/NNNN: its name is at NPtr, NLen long.
DoDrop      stz   PathF            ; Inbox/NNNN
            Append PathF;InboxSlash+2;6
            ldx   #PathF
            ldy   NPtr
            lda   NLen
            jsr   AppendMem
            SetPtr PathF;$04
            SetPtr List2;$08
            SetVal 1024;$0C
            SetVal 0;$10
            lda   #$0010           ; LIST the folder
            jsr   BCall
            beq   :ok
            rts
:ok         lda   Block+$14
            sta   Ent2Left
            stz   HaveTarget
            stz   DLen
            lda   #List2
            sta   Ent2Ptr
:next       lda   Ent2Left
            beq   :scanned
            lda   Ent2Ptr
            sta   Src
            ldy   #2
            lda   (Src),y          ; flags, type
            sta   Tmp
            ldy   #10
            lda   (Src),y          ; name length
            sta   Cnt
            lda   Ent2Ptr
            clc
            adc   #12
            sta   Base             ; name address
            ldy   #0
            lda   (Src),y
            clc
            adc   Ent2Ptr
            pha                    ; the next entry
            dec   Ent2Left
            lda   Tmp
            and   #$0001
            bne   :nextent         ; a folder inside: ignore
            lda   Cnt
            cmp   #6
            bne   :data
            jsr   IsTarget
            bne   :data
            lda   #1
            sta   HaveTarget
            bra   :nextent
:data       ldy   #4
            lda   (Src),y          ; aux (only of the data file, not target)
            sta   DAux
            lda   Cnt
            sta   DLen
            lda   Base
            sta   DPtr
            lda   Tmp
            xba
            and   #$00FF
            sta   DType
:nextent    pla
            sta   Ent2Ptr
            bra   :next
:scanned    lda   HaveTarget
            bne   :t
            rts                    ; no target yet: not finished being staged
:t          lda   DLen
            bne   :d
            rts
:d          jsr   ReadTarget
            beq   :target
            rts
:target     stz   ErrCode
* the path of the new file: target + ":" + name
            stz   DestBuf
            ldx   #DestBuf
            ldy   #TgtBuf+2
            lda   TgtBuf
            jsr   AppendMem
            Append DestBuf;Colon+2;1
            ldx   #DestBuf
            ldy   DPtr
            lda   DLen
            jsr   AppendMem
* Inbox/NNNN/<name> (the staged data file)
            stz   PathH
            Append PathH;InboxSlash+2;6
            ldx   #PathH
            ldy   NPtr
            lda   NLen
            jsr   AppendMem
            Append PathH;Slash+2;1
            ldx   #PathH
            ldy   DPtr
            lda   DLen
            jsr   AppendMem
* create the file
            LongPtr CrPB;2;DestBuf
            lda   #$00C3
            sta   CrPB+6           ; access
            lda   DType
            sta   CrPB+8           ; file type
            lda   DAux
            sta   CrPB+10          ; aux type (long)
            stz   CrPB+12
            lda   #1
            sta   CrPB+14          ; storage type
            GSOS  $2001;CrPB
            bcc   :created
            sta   ErrCode
            jmp   Report
:created    LongPtr OpPB;4;DestBuf
            lda   #2
            sta   OpPB+8           ; write
            GSOS  $2010;OpPB
            bcc   :opened
            sta   ErrCode
            jmp   Report
:opened     lda   OpPB+2
            sta   RefDst
* open the staged file through the bridge
            SetPtr PathH;$04
            SetVal 1;$08
            lda   #$0020           ; OPEN (read)
            jsr   BCall
            beq   :bopen
            sta   ErrCode
            jmp   CloseDst
:bopen      lda   Block+$14
            sta   HandIn
:copy       lda   HandIn
            sta   Block+$04
            stz   Block+$06
            SetBuf $08
            SetVal 16384;$0C
            lda   #$0021           ; READ
            jsr   BCall
            beq   :read
            sta   ErrCode
            bra   :closein
:read       lda   Block+$14
            beq   :closein         ; the end
            sta   Count
            lda   RefDst
            sta   WrPB+2
            lda   BufPtr
            sta   WrPB+4
            lda   BufPtr+2
            sta   WrPB+6
            lda   Count
            sta   WrPB+8
            stz   WrPB+10
            GSOS  $2013;WrPB
            bcc   :copy
            sta   ErrCode
:closein    lda   HandIn
            sta   Block+$04
            stz   Block+$06
            lda   #$0023           ; CLOSE
            jsr   BCall
CloseDst    lda   RefDst
            sta   ClPB+2
            GSOS  $2014;ClPB
            lda   ErrCode
            bne   Report
* done: remove the staged files (data, target, folder)
            SetPtr PathH;$04
            lda   #$0012
            jsr   BCall
            stz   PathG
            Append PathG;InboxSlash+2;6
            ldx   #PathG
            ldy   NPtr
            lda   NLen
            jsr   AppendMem
            Append PathG;TargetName+2;7
            SetPtr PathG;$04
            lda   #$0012
            jsr   BCall
            SetPtr PathF;$04
            lda   #$0012
            jsr   BCall
Report      stz   ResBuf           ; the note: OK path / ERR $xx path
            lda   ErrCode
            bne   :err
            Append ResBuf;OkText+2;3
            bra   :path
:err        Append ResBuf;ErrText+2;5
            lda   ErrCode
            jsr   ToHex
            Append ResBuf;HexTmp;2
            Append ResBuf;Space+2;1
:path       ldx   #ResBuf
            ldy   #DestBuf+2
            lda   DestBuf
            jsr   AppendMem
            Append ResBuf;CR+2;1
            stz   PathN            ; Outbox/NNNN
            Append PathN;OutSlash+2;7
            ldx   #PathN
            ldy   NPtr
            lda   NLen
            jsr   AppendMem
            SetPtr PathN;$04
            SetVal 2;$08
            SetVal 4;$0C           ; text
            SetVal 0;$10
            lda   #$0020           ; OPEN (write)
            jsr   BCall
            bne   :end
            lda   Block+$14
            sta   HandIn
            sta   Block+$04
            stz   Block+$06
            SetPtr ResBuf+2;$08
            lda   ResBuf
            sta   Block+$0C
            stz   Block+$0E
            lda   #$0022           ; WRITE
            jsr   BCall
            lda   HandIn
            sta   Block+$04
            stz   Block+$06
            lda   #$0023           ; CLOSE
            jsr   BCall
:end        rts

* Z set if the name at Base (6 characters) is "target"
IsTarget    sep   #$20
            ldy   #0
:lp         lda   (Base),y
            cmp   TargetName+3,y
            bne   :no
            iny
            cpy   #6
            bne   :lp
            rep   #$20
            lda   #0
            rts
:no         rep   #$20
            lda   #1
            rts

* Read Inbox/NNNN/target.txt into TgtBuf (length word + the path). A = 0 if ok.
ReadTarget  stz   PathG
            Append PathG;InboxSlash+2;6
            ldx   #PathG
            ldy   NPtr
            lda   NLen
            jsr   AppendMem
            Append PathG;TargetName+2;7
            SetPtr PathG;$04
            SetVal 1;$08
            lda   #$0020           ; OPEN (read)
            jsr   BCall
            beq   :open
            lda   #1
            rts
:open       lda   Block+$14
            sta   HandIn
            sta   Block+$04
            stz   Block+$06
            SetPtr TgtRaw;$08
            SetVal 250;$0C
            lda   #$0021           ; READ
            jsr   BCall
            lda   Block+$14
            sta   Count
            lda   HandIn
            sta   Block+$04
            stz   Block+$06
            lda   #$0023           ; CLOSE
            jsr   BCall
            stz   TgtBuf
            lda   Count
            beq   :bad
            ldy   #0
            sep   #$20
:cp         cpy   Count
            beq   :end
            lda   TgtRaw,y
            cmp   #$0D
            beq   :end
            cmp   #$0A
            beq   :end
            sta   TgtBuf+2,y
            iny
            bra   :cp
:end        rep   #$20
            tya
            sta   TgtBuf
            beq   :bad
            lda   #0
            rts
:bad        rep   #$30
            lda   #1
            rts

*-------------------------------------------------------------
UserID      dw    0
BufPtr      adrl  0

QuitParms   dw    2
            adrl  0
            dw    0

CrPB        dw    5                ; Create: path, access, type, aux, storage
            adrl  0
            dw    0,0
            adrl  0
            dw    0
OpPB        dw    3                ; Open: reference (out), path, access
            dw    0
            adrl  0
            dw    0
WrPB        dw    4                ; Write: reference, buffer, count, transferred
            dw    0
            adrl  0,0,0
ClPB        dw    1                ; Close
            dw    0

Block       dw    0                ; the bridge's parameter block
            dw    0
            adrl  0,0,0,0
            adrl  0,0,0,0

PathInbox   dw    5
            asc   'Inbox'
PathOutbox  dw    6
            asc   'Outbox'
InboxSlash  dw    6
            asc   'Inbox/'
OutSlash    dw    7
            asc   'Outbox/'
TargetName  dw    7
            asc   '/target'
Slash       dw    1
            asc   '/'
Colon       dw    1
            asc   ':'
Space       dw    1
            asc   ' '
CR          dw    1
            dfb   $0D
OkText      dw    3
            asc   'OK '
ErrText     dw    5
            asc   'ERR $'
HexDigits   asc   '0123456789ABCDEF'
HexTmp      asc   '00'

PathF       ds    300
PathG       ds    300
PathH       ds    300
PathN       ds    300
DestBuf     ds    300
TgtBuf      ds    260
TgtRaw      ds    260
ResBuf      ds    300
List2       ds    1024
ListBuf     ds    4096
