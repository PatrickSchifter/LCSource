#!/usr/bin/env python3
"""Minimal 2009 client: logs in through the LoginServer and the GameServer, prints what comes back.

    v2009/scripts/test-login.py [--host 127.0.0.1] [--login-port 4201] [--user teste2009] [--password teste2009]

Wire format (v2009/Server Source, CRYPT_NET_MSG + COMPRESSION_MESSAGE + SECRET_KEY, as the client):
  frame     12-byte header, big-endian: u16 reliable (0x0181), u32 seq, u16 id, u32 size; then size bytes
  client -> server  plaintext [type][body] is encrypted by LCCrypt (CNM_Crypt with the fixed key
            CNM_INIT_KEY: the 2009 client never changes keys) into [0x01 dummy][encrypted 'l' body 'c'
            crc16]; the client then "packs" it: type byte = 0x01 | 2 << 6 (no compression)
  server -> client  [0x01][encrypted ...], same cipher and key, not packed
  integers big-endian, strings NUL-terminated
MSG_LOGIN: version(u32) mode(u8, 0 = new login) id(str) pw(str) nation(u8) seed(u32).
"""
import argparse
import pathlib
import re
import socket
import struct
import sys

CRYPT_DIR = pathlib.Path(__file__).resolve().parent.parent / "Server Source" / "LCCrypt"
INIT_KEY = 1852535664  # CNM_INIT_KEY ("nksp")

MSG_FAIL, MSG_DB, MSG_LOGIN, MSG_MENU, MSG_START_GAME, MSG_APPEAR, MSG_LOGINSERV_PLAYER = 1, 2, 3, 4, 5, 7, 34
MSG_MENU_NEW, MSG_MENU_START = 0, 2
MSG_DB_NAMES = {0: "MSG_DB_SUCCESS", 1: "MSG_DB_OK", 2: "MSG_DB_CHAR_EXIST", 3: "MSG_DB_CHAR_END",
                4: "MSG_DB_OTHER_SERVER"}


def table(name):
    text = (CRYPT_DIR / name).read_text()
    values = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", text)]
    assert len(values) == 256, name
    return values


TRANS, RTRANS = table("LCCrypt_TransTable"), table("LCCrypt_TransTable_Reverse")


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8003) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def key_bytes(key):
    return [(key >> 24) & 0xFF, (key >> 16) & 0xFF, (key >> 8) & 0xFF, key & 0xFF]


def crypt(plain, key=INIT_KEY):
    buf = bytearray([1, ord("l")]) + plain + bytes([ord("c")])
    crc = crc16(buf[1:])
    buf += bytes([crc >> 8, crc & 0xFF])
    k = key_bytes(key)
    for i in range(1, len(buf)):
        t = TRANS[buf[i]] ^ k[i % 4]
        t = ((t << 7) & 0x80) | ((t >> 1) & 0x7F)
        k[i % 4] = t
        buf[i] = t
    return bytes(buf)


def decrypt(data, key=INIT_KEY):
    k = key_bytes(key)
    out = bytearray(len(data))
    for i in range(1, len(data)):
        t = data[i]
        t = ((t >> 7) & 0x01) | ((t << 1) & 0xFE)
        t = RTRANS[t ^ k[i % 4]]
        k[i % 4] = data[i]
        out[i] = t
    if out[1] != ord("l") or out[-3] != ord("c"):
        raise ValueError("bad marks")
    if ((out[-2] << 8) | out[-1]) != crc16(out[1:-2]):
        raise ValueError("bad checksum")
    return bytes(out[2:-3])


class Conn:
    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=10)
        self.seq = 0
        self.buf = b""

    def send(self, plain):
        enc = crypt(plain)
        packed = bytes([enc[0] | (2 << 6)]) + enc[1:]
        header = struct.pack(">HIHI", 0x0181, self.seq, 0, len(packed))
        self.seq += 1
        self.sock.sendall(header + packed)

    def recv(self):
        while True:
            if len(self.buf) >= 12:
                size = struct.unpack(">I", self.buf[8:12])[0]
                if len(self.buf) >= 12 + size:
                    body, self.buf = self.buf[12:12 + size], self.buf[12 + size:]
                    return decrypt(body)
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed by server")
            self.buf += chunk


class Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def u8(self):
        self.p += 1
        return self.d[self.p - 1]

    def i32(self):
        self.p += 4
        return struct.unpack(">i", self.d[self.p - 4:self.p])[0]

    def str(self):
        end = self.d.index(0, self.p)
        s, self.p = self.d[self.p:end].decode("latin-1"), end + 1
        return s


def login_msg(user, password, version, nation):
    return (bytes([MSG_LOGIN]) + struct.pack(">IB", version, 0) + user.encode() + b"\0"
            + password.encode() + b"\0" + struct.pack(">BI", nation, 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--login-port", type=int, default=4201)
    ap.add_argument("--user", default="teste2009")
    ap.add_argument("--password", default="teste2009")
    ap.add_argument("--version", type=int, default=1648)
    ap.add_argument("--nation", type=int, default=10)
    ap.add_argument("--create", metavar="NAME", help="create a character (titan) if the list is empty")
    ap.add_argument("--start", action="store_true", help="enter the game with the first character")
    args = ap.parse_args()

    print(f"LoginServer {args.host}:{args.login_port}")
    c = Conn(args.host, args.login_port)
    c.send(login_msg(args.user, args.password, args.version, args.nation))
    servers = []
    try:
        while True:
            m = c.recv()
            r = Reader(m)
            t = r.u8()
            if t == MSG_FAIL:
                sys.exit(f"  MSG_FAIL {r.u8()}")
            if t != MSG_LOGINSERV_PLAYER:
                print(f"  message type {t}: {m[1:].hex()}")
                continue
            recent, recent_sub, count, seq, serverno, nsub = (r.i32() for _ in range(6))
            recommended = r.u8()  # RECOMMEND_SERVER_SYSTEM
            print(f"  MSG_LOGINSERV_PLAYER {seq}/{count}: group {serverno}, {nsub} sub-server(s)"
                  f"{', recommended' if recommended else ''}")
            for _ in range(max(nsub, 0)):
                sub, players, ip, port = r.i32(), r.i32(), r.str(), r.i32()
                print(f"    sub {sub}: {ip}:{port} players {players - 1999}")
                servers.append((ip, port))
            if seq >= count:
                break
    except socket.timeout:
        pass
    c.sock.close()
    if not servers:
        sys.exit("no game server listed")

    ip, port = servers[0]
    print(f"GameServer {ip}:{port}")
    g = Conn(ip, port)
    g.send(login_msg(args.user, args.password, args.version, args.nation))
    chars = read_char_list(g)
    if not chars and args.create:
        print(f"creating {args.create}")
        g.send(bytes([MSG_MENU, MSG_MENU_NEW]) + args.create.encode() + b"\0" + bytes([0, 1, 1]))
        chars = read_char_list(g)
    if args.start and chars:
        index, name = chars[0]
        print(f"starting {name} ({index})")
        g.send(bytes([MSG_MENU, MSG_MENU_START]) + struct.pack(">i", index))
        while True:
            m = g.recv()
            r = Reader(m)
            t = r.u8()
            if t == MSG_FAIL:
                sys.exit(f"  MSG_FAIL {r.u8()}")
            if t == MSG_DB and r.u8() == 1:  # MSG_DB_OK: zone(n)
                print(f"  MSG_DB_OK zone {r.i32()}; sending MSG_START_GAME")
                g.send(bytes([MSG_START_GAME]))
                break
        seen = {}
        try:
            while sum(seen.values()) < 200:
                t = g.recv()[0]
                seen[t] = seen.get(t, 0) + 1
        except socket.timeout:
            pass
        print("  in game; message types received:", dict(sorted(seen.items())))
        if MSG_APPEAR in seen:
            print(f"  {seen[MSG_APPEAR]} MSG_APPEAR (NPCs / players around)")


def read_char_list(g):
    chars = []
    try:
        while True:
            m = g.recv()
            r = Reader(m)
            t = r.u8()
            if t == MSG_FAIL:
                sys.exit(f"  MSG_FAIL {r.u8()}")
            if t != MSG_DB:
                print(f"  message type {t} ({len(m)} bytes)")
                continue
            sub = r.u8()
            name = MSG_DB_NAMES.get(sub, sub)
            if name == "MSG_DB_CHAR_EXIST":
                index, cname = r.i32(), r.str()
                print(f"  MSG_DB_CHAR_EXIST {index} {cname}")
                chars.append((index, cname))
            elif name == "MSG_DB_CHAR_END":
                print(f"  MSG_DB_CHAR_END: character list reached ({len(chars)} characters)")
                return chars
            else:
                print(f"  MSG_DB {name}")
    except socket.timeout:
        sys.exit("timed out waiting for the character list")

if __name__ == "__main__":
    main()
