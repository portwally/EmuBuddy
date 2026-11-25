#!/usr/bin/env python3
"""Phase 0 of docs/FUJINET_PLAN.md: plays the emulator's side of SmartPort
over SLIP. Listens on 127.0.0.1:1985, waits for FujiNet-PC, sends INIT to
unit 1, 2, ... and then STATUS (code 0 and code 3, the DIB) to each unit."""
import socket, sys, time
END, ESC, ESC_END, ESC_ESC = 0xC0, 0xDB, 0xDC, 0xDD
def enc(b):
    out = bytearray([END])
    for c in b:
        out += bytes([ESC, ESC_END]) if c == END else bytes([ESC, ESC_ESC]) if c == ESC else bytes([c])
    return bytes(out + bytes([END]))
class Peer:
    def __init__(self, s): self.s, self.buf, self.seq = s, bytearray(), 0
    def frame(self, timeout):
        self.s.settimeout(timeout)
        end = time.time() + timeout
        while True:
            # a frame: END, bytes, END
            while self.buf and self.buf[0] != END: del self.buf[0]
            i = self.buf.find(bytes([END]), 1)
            if i > 1:
                raw = bytes(self.buf[1:i]); del self.buf[:i + 1]
                out, k = bytearray(), 0
                while k < len(raw):
                    if raw[k] == ESC and k + 1 < len(raw):
                        out.append(END if raw[k + 1] == ESC_END else ESC); k += 2
                    else:
                        out.append(raw[k]); k += 1
                return bytes(out)
            if i == 1: del self.buf[0]; continue
            if time.time() > end: return None
            try: d = self.s.recv(4096)
            except socket.timeout: return None
            if not d: return None
            self.buf += d
    def call(self, cmd, unit, params=b'', payload=b'', pc=1, timeout=3):
        self.seq = (self.seq + 1) & 255
        req = bytes([self.seq, cmd, pc, unit, 0, 0]) + params.ljust(5, b'\0') + payload
        self.s.sendall(enc(req))
        while True:
            r = self.frame(timeout)
            if r is None: return None
            if r and r[0] == self.seq: return r
port = int(sys.argv[1]) if len(sys.argv) > 1 else 1985
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(('127.0.0.1', port)); ls.listen(1); ls.settimeout(30)
print('listening on 127.0.0.1:%d' % port, flush=True)
s, addr = ls.accept(); print('connected from', addr, flush=True)
p = Peer(s); n = 0
for unit in range(1, 40):
    r = p.call(5, unit)
    print('INIT unit %d ->' % unit, r.hex(' ') if r else 'no answer', flush=True)
    if r is None: break
    n = unit
    if r[1] != 0: break
print('units:', n)
for unit in range(1, n + 1):
    for code in (0, 3):
        r = p.call(0, unit, bytes([code]), pc=3)
        if r is None: print('unit %d STATUS %d: no answer' % (unit, code)); continue
        d = r[2:]
        if code == 3 and r[1] == 0 and len(d) >= 25:
            blocks = d[1] | d[2] << 8 | d[3] << 16
            name = d[5:5 + d[4]].decode('ascii', 'replace')
            print('unit %2d DIB: status %02x blocks %6d name %-16s type %02x subtype %02x version %02x%02x' % (unit, d[0], blocks, name, d[21], d[22], d[24], d[23]))
        else:
            print('unit %2d STATUS %d: err %02x data %s' % (unit, code, r[1], d.hex(' ')))
r = p.call(5, 1, timeout=2); print('second INIT round, unit 1 ->', r.hex(' ') if r else 'no answer')
r = p.call(1, n, bytes([0, 0, 0]), pc=3); print('READ BLOCK 0 of the last unit ->', ('err %02x, %d bytes' % (r[1], len(r) - 2)) if r else 'no answer')
s.close()
