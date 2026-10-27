#!/usr/bin/env python3
"""Testează firmware-ul STM32 de pe PC, fără ESP32, prin portul serial USB al plăcii.

Placa trebuie scrisă cu protocolul pe USB:   make LINK=vcp flash
Apoi:                                        python tools/vcp_test.py COM5      (Windows)
                                             python3 tools/vcp_test.py /dev/ttyACM0

Verifică, pe rând: citirile periodice L,<raw>,<pct>,<stable>, citirea la cerere (R), calibrarea
acceptată (C,a,b -> A,C) și respinsă (C,a,a -> E,C), procentul calculat după calibrare și starea
conexiunii (S,1 / S,0 -> LED-ul verde LD3 și textul „BLE” de pe LCD, verificate cu ochiul).
Are nevoie de pyserial:  pip install pyserial
"""
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit('Lipsește pyserial: pip install pyserial')

LEVEL = re.compile(r'^L,(-?\d+),(\d{1,3}),([01])$')


def pick_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in list_ports.comports():
        if 'STLink' in (p.description or '') or 'STMicro' in (p.manufacturer or '') or p.vid == 0x0483:
            return p.device
    sys.exit('Nu găsesc placa. Dă portul ca argument, de ex.: python tools/vcp_test.py COM5')


class Board:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.2)
        self.buf = b''

    def send(self, line):
        self.s.write((line + '\n').encode())

    def lines(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            self.buf += self.s.read(256)
            while b'\n' in self.buf:
                raw, self.buf = self.buf.split(b'\n', 1)
                yield raw.decode(errors='replace').strip()

    def wait_for(self, pred, seconds):
        for ln in self.lines(seconds):
            if ln and pred(ln):
                return ln
        return None


def main():
    # consola Windows (cp1252) nu poate afișa diacriticele
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    port = pick_port()
    print(f'Port: {port}')
    b = Board(port)
    ok = True

    def check(name, cond, detail=''):
        nonlocal ok
        ok &= bool(cond)
        print(f"  {'OK  ' if cond else 'EȘEC'} {name}{(': ' + detail) if detail else ''}")

    print('1. Citiri periodice (aștept 3 în ~4 s)')
    got = [m for m in (LEVEL.match(l) for l in b.lines(4.5)) if m]
    check('L,raw,pct,stable la ~1 s', len(got) >= 3, f'{len(got)} citiri, ultima raw={got[-1].group(1) if got else "-"}')
    if not got:
        print('\nNu vine nimic. Placa e scrisă cu „make LINK=vcp flash”? Portul e cel bun?')
        sys.exit(1)

    print('2. Citire la cerere')
    b.send('R')
    check('R -> L imediat', b.wait_for(LEVEL.match, 0.8) is not None)

    print('3. Calibrare')
    b.send('C,5,5')
    check('C,5,5 respinsă', b.wait_for(lambda l: l in ('A,C', 'E,C'), 1.5) == 'E,C')
    raw = int(got[-1].group(1))
    b.send(f'C,{raw - 100},{raw + 100}')        # citirea curentă ar trebui să fie la ~50%
    check('C,a,b acceptată', b.wait_for(lambda l: l in ('A,C', 'E,C'), 1.5) == 'A,C')
    m = LEVEL.match(b.wait_for(LEVEL.match, 2.5) or '')
    check('procent după calibrare ~50%', m and 30 <= int(m.group(2)) <= 70, m.group(2) + '%' if m else 'fără citire')

    print('4. Starea conexiunii BLE (verifică pe placă)')
    b.send('S,1')
    input('   LED-ul verde LD3 e aprins și pe LCD scrie „BLE: conectat”? Apasă Enter... ')
    b.send('S,0')
    print('   (am trimis S,0: LED-ul se stinge)')

    print('\n' + ('Toate verificările au trecut.' if ok else 'Unele verificări au eșuat.'))
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
