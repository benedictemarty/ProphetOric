#!/usr/bin/env python3
"""Relais TCP sur la PREMIÈRE réponse d'un fichier (/files/<id>/<n>, avec ou sans Range),
puis tout passe tel quel. Mode `cut` (défaut) : coupe après CUT octets de corps (reprise
par Range). Mode `flip` : inverse l'octet de corps n° CUT et transmet le reste (détection
par CRC-32). Usage : cut_proxy.py <port_ecoute> <port_prophetd> <CUT> [cut|flip]"""
import socket, sys, threading
LISTEN, TARGET, CUT = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
MODE = sys.argv[4] if len(sys.argv) > 4 else "cut"
cut_done = False

def handle(c):
    global cut_done
    req = b""
    while b"\r\n\r\n" not in req:
        d = c.recv(4096)
        if not d: c.close(); return
        req += d
    first = req.split(b"\r\n", 1)[0]
    s = socket.create_connection(("127.0.0.1", TARGET)); s.sendall(req)
    cut = (not cut_done) and b" /files/" in first and first.split(b" ")[1].count(b"/") == 3
    if cut: cut_done = True
    sent_body = 0; hdr_done = False; buf = b""
    while True:
        d = s.recv(4096)
        if not d: break
        if not cut: c.sendall(d); continue
        buf += d
        if not hdr_done:
            i = buf.find(b"\r\n\r\n")
            if i < 0: continue
            hdr_done = True; c.sendall(buf[:i + 4]); buf = buf[i + 4:]
        if MODE == "flip":
            if sent_body <= CUT < sent_body + len(buf):
                k = CUT - sent_body; buf = buf[:k] + bytes([buf[k] ^ 0xFF]) + buf[k + 1:]
            c.sendall(buf); sent_body += len(buf); buf = b""; continue
        room = CUT - sent_body
        if room <= 0: break
        c.sendall(buf[:room]); sent_body += min(room, len(buf)); buf = b""
        if sent_body >= CUT: break
    s.close(); c.close()

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", LISTEN)); srv.listen(5)
while True:
    c, _ = srv.accept(); threading.Thread(target=handle, args=(c,), daemon=True).start()
