#!/usr/bin/env python3

import socket, threading, time, datetime, itertools, random

HOST, PORT      = "0.0.0.0", 7777
MAX_THINK_TIME  = 120.0     # random 0..this seconds between cycles
CONFIRM_TIMEOUT = 1800.0    # give up waiting for a confirmation after 30 min
DELAY_FLAG_S    = 5.0       # flag any confirmation slower than this
PAYLOAD_SIZE    = 400

def ts():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()

def recv_confirmation(conn, expect_len, deadline):
    got = b""
    while len(got) < expect_len:
        conn.settimeout(max(0.1, deadline - time.monotonic()))
        try:
            chunk = conn.recv(4096)
        except socket.timeout:
            return None
        if not chunk:
            return b""
        got += chunk
    return got

def handle(conn, addr):
    print(f"{ts()}  [{addr}] CONNECTED", flush=True)

    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    try:
        for n in itertools.count(1):
            conn.settimeout(None)

            body = f"MTDATA #{n} {ts()} ".encode()
            payload = body + b"X" * max(0, PAYLOAD_SIZE - len(body))
            t_send = time.monotonic()

            print(f"{ts()}  [{addr}] TX #{n} {len(payload)}B -> awaiting confirmation", flush=True)

            conn.sendall(payload)
            conf = recv_confirmation(conn, len(payload), t_send + CONFIRM_TIMEOUT)
            delay = time.monotonic() - t_send

            if conf is None:
                print(f"{ts()}  [{addr}] !! #{n} NO confirmation within {CONFIRM_TIMEOUT:.0f}s; closing", flush=True)

                return

            if conf == b"":
                print(f"{ts()}  [{addr}] peer closed while awaiting #{n}", flush=True)

                return

            flag = "  <<<<< DELAYED" if delay > DELAY_FLAG_S else ""

            print(f"{ts()}  [{addr}] CONFIRMED #{n} after {delay:6.1f}s{flag}", flush=True)
            time.sleep(random.uniform(0, MAX_THINK_TIME))

    except OSError as e:
        print(f"{ts()}  [{addr}] socket error: {e}", flush=True)

    finally:
        conn.close()
        print(f"{ts()}  [{addr}] CLOSED", flush=True)

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)
    print(f"{ts()}  listening {HOST}:{PORT}; send-on-confirm, random think <= {MAX_THINK_TIME:.0f}s", flush=True)

    while True:
        conn, addr = srv.accept()
        threading.Thread(target=handle, args=(conn, addr), daemon=True).start()

if __name__ == "__main__":
    main()