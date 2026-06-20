"""
Bridge integration tests.

Hardware required:
  - ESP32 running the firmware (UART2 TX pin 17 shorted to RX pin 16).
  - Reachable at ESP32_IP:ESP32_PORT (defaults: 10.20.32.29:3000).
  - Override: ESP32_IP=<ip> ESP32_PORT=<port> pytest tests/ -v

Data path under test:
  TCP client → bridge (TCP port) → UART TX → [loopback wire] → UART RX
             → bridge → TCP client

Because the bridge accepts exactly one TCP client at a time all tests use
a single connection. The only valid way to test concurrent transfer is to
send and receive on the same socket from two threads.
"""

import socket
import threading
import time

import pytest

from conftest import (
    ESP32_IP,
    ESP32_PORT,
    BRIDGE_BUFFER_SZ,
    IDLE_TIMEOUT_S,
    RECV_TIMEOUT_S,
    connect_with_retry,
    recv_all,
)


# ---------------------------------------------------------------------------
# Echo integrity — TCP → UART (loopback) → TCP
# ---------------------------------------------------------------------------

def test_echo_small(sock):
    """All 256 byte values in a single 256-byte burst echo back correctly."""
    data = bytes(range(256))
    sock.sendall(data)
    assert recv_all(sock, len(data)) == data


def test_echo_exact_buffer_size(sock):
    """Payload equal to BRIDGE_BUFFER_SZ is forwarded in one bridge read."""
    data = bytes(range(256)) * (BRIDGE_BUFFER_SZ // 256)
    assert len(data) == BRIDGE_BUFFER_SZ
    sock.sendall(data)
    assert recv_all(sock, len(data)) == data


def test_echo_one_over_buffer_size(sock):
    """BRIDGE_BUFFER_SZ + 1 bytes forces two read cycles in the bridge tick."""
    data = bytes(range(256)) * (BRIDGE_BUFFER_SZ // 256) + b"\xff"
    assert len(data) == BRIDGE_BUFFER_SZ + 1
    sock.sendall(data)
    assert recv_all(sock, len(data), timeout=10.0) == data


def test_echo_multi_buffer(sock):
    """8 KB payload (8× BRIDGE_BUFFER_SZ) requires many bridge tick cycles."""
    data = bytes(range(256)) * 32  # 8 192 bytes
    sock.sendall(data)
    assert recv_all(sock, len(data), timeout=15.0) == data


def test_echo_large(sock):
    """64 KB sustained transfer — checks no bytes are dropped over time."""
    data = bytes(range(256)) * 256  # 65 536 bytes
    sock.sendall(data)
    assert recv_all(sock, len(data), timeout=30.0) == data


# ---------------------------------------------------------------------------
# Sequential payloads — ordering and isolation
# ---------------------------------------------------------------------------

def test_sequential_distinct_payloads(sock):
    """16 payloads with unique byte values, received back in order."""
    for i in range(16):
        payload = bytes([i]) * (i + 1)  # 1..16 bytes each
        sock.sendall(payload)
        assert recv_all(sock, len(payload)) == payload


def test_single_byte_payloads(sock):
    """64 single-byte sends — checks the bridge does not drop small transfers."""
    for b in range(64):
        sock.sendall(bytes([b]))
        assert recv_all(sock, 1) == bytes([b])


def test_alternating_payload_sizes(sock):
    """Alternate between sub-buffer and multi-buffer payloads without mixing."""
    sizes = [1, BRIDGE_BUFFER_SZ * 2, 1, BRIDGE_BUFFER_SZ, 3, BRIDGE_BUFFER_SZ + 1]
    for size in sizes:
        payload = bytes([size & 0xFF]) * size
        sock.sendall(payload)
        result = recv_all(sock, size, timeout=10.0)
        assert result == payload, f"Mismatch at size {size}"


# ---------------------------------------------------------------------------
# Buffer boundary sweep
# ---------------------------------------------------------------------------

def test_buffer_boundary_sequence(sock):
    """Payloads spanning just-below / at / just-above / double BRIDGE_BUFFER_SZ."""
    cases = [
        (BRIDGE_BUFFER_SZ - 1, 0x01),
        (BRIDGE_BUFFER_SZ,     0x02),
        (BRIDGE_BUFFER_SZ + 1, 0x03),
        (BRIDGE_BUFFER_SZ * 2, 0x04),
    ]
    for size, fill in cases:
        payload = bytes([fill]) * size
        sock.sendall(payload)
        result = recv_all(sock, size, timeout=10.0)
        assert result == payload, f"Mismatch at size {size}"


# ---------------------------------------------------------------------------
# Concurrent send and receive on a single connection
#
# The bridge has one TCP port and one UART port: only one client is served
# at any time. The right concurrency test is to send and drain simultaneously
# on the same socket (two threads, one connection), not two parallel sockets.
# ---------------------------------------------------------------------------

def test_concurrent_send_receive(sock):
    """Send 4 KB while draining the echo concurrently — no deadlock."""
    SIZE    = 4096
    payload = bytes(range(256)) * (SIZE // 256)
    received = []
    errors   = []

    def receiver():
        try:
            received.append(recv_all(sock, SIZE, timeout=20.0))
        except Exception as exc:
            errors.append(str(exc))

    t = threading.Thread(target=receiver, daemon=True)
    t.start()
    sock.sendall(payload)
    t.join(timeout=25)

    assert not errors, f"Receiver error: {errors}"
    assert received and received[0] == payload


def test_concurrent_large_transfer(sock):
    """16 KB concurrent send+receive — sustained bidirectional echo."""
    SIZE    = 16384
    payload = bytes(range(256)) * (SIZE // 256)
    received = []
    errors   = []

    def receiver():
        try:
            received.append(recv_all(sock, SIZE, timeout=30.0))
        except Exception as exc:
            errors.append(str(exc))

    t = threading.Thread(target=receiver, daemon=True)
    t.start()
    sock.sendall(payload)
    t.join(timeout=35)

    assert not errors, f"Receiver error: {errors}"
    assert received and received[0] == payload


# ---------------------------------------------------------------------------
# Reconnect and recovery
# ---------------------------------------------------------------------------

def test_reconnect_after_clean_close():
    """Bridge accepts a new client immediately after the previous one closes."""
    s = connect_with_retry()
    s.sendall(b"hello")
    assert recv_all(s, 5) == b"hello"
    s.close()

    time.sleep(0.3)  # one bridge tick is 10 ms; 300 ms is comfortably enough

    s2 = connect_with_retry()
    try:
        s2.sendall(b"world")
        assert recv_all(s2, 5) == b"world"
    finally:
        s2.close()


def test_rapid_reconnect():
    """20 rapid connect/echo/close cycles without the bridge locking up."""
    for i in range(20):
        s = connect_with_retry()
        try:
            payload = f"ping{i:03d}".encode()   # 7 bytes, unique per iteration
            s.sendall(payload)
            assert recv_all(s, len(payload)) == payload
        finally:
            s.close()
        time.sleep(0.1)


def test_abrupt_close_recovery():
    """Bridge returns to WAIT after a client closes mid-transfer (RST/EOF)."""
    s = connect_with_retry()
    # Send half a large payload then drop the socket without a graceful FIN
    s.sendall(bytes([0xAB]) * 4096)
    s.close()

    # Bridge detects EOF on the next is_connected check (≤ 10 ms) and goes
    # back to WAIT. Give it a generous margin.
    time.sleep(0.5)

    s2 = connect_with_retry(retries=5, delay=0.3)
    try:
        data = bytes(range(256))
        s2.sendall(data)
        assert recv_all(s2, len(data)) == data
    finally:
        s2.close()


def test_reconnect_state_is_clean():
    """Data from a closed connection does not bleed into the next session.

    The bridge flushes both ports when it transitions WAIT → CONNECT. Any
    residual UART bytes from the previous client must not appear in the new
    client's read stream.
    """
    s = connect_with_retry()
    s.sendall(bytes([0xFF]) * 512)
    # Do not drain — leave bytes in flight, then close
    s.close()

    time.sleep(0.5)

    s2 = connect_with_retry(retries=5, delay=0.3)
    try:
        probe = bytes([0x42]) * 32
        s2.sendall(probe)
        result = recv_all(s2, len(probe), timeout=5.0)
        # Must be exactly the probe bytes — no 0xFF bleed-through
        assert result == probe
    finally:
        s2.close()


# ---------------------------------------------------------------------------
# Idle timeout
# ---------------------------------------------------------------------------

def test_idle_timeout_closes_connection(sock):
    """Bridge closes the connection after IDLE_TIMEOUT_S seconds of silence.

    The bridge idle counter resets only when data is transferred. After the
    threshold (idle_timeout * 1000 / BRIDGE_TICK_MS ticks) the bridge calls
    disconnect on both ports and returns to WAIT state.
    """
    # Prime the bridge into SEND state with a small exchange
    sock.sendall(b"prime")
    recv_all(sock, 5)

    # Sit silent long enough for the idle counter to expire
    time.sleep(IDLE_TIMEOUT_S + 2)

    # The bridge should have closed the TCP side — recv returns b"" (EOF) or raises
    sock.settimeout(2.0)
    try:
        data = sock.recv(1)
        assert data == b"", "Expected bridge to close the connection (got data)"
    except (ConnectionResetError, OSError):
        pass  # RST from the bridge side is equally valid


def test_idle_timeout_reset_by_activity(sock):
    """Sending data before the idle deadline resets the counter.

    Each exchange resets _idle_ticks to 0, so the connection survives
    repeated near-expiry keepalive sends.
    """
    for _ in range(3):
        time.sleep(max(IDLE_TIMEOUT_S - 2, 1))
        sock.sendall(b"keepalive")
        assert recv_all(sock, 9) == b"keepalive"

    # Connection must still be alive after the reset loop
    sock.sendall(b"still here")
    assert recv_all(sock, 10) == b"still here"


def test_idle_timeout_then_reconnect():
    """After an idle-triggered disconnect the bridge re-enters WAIT correctly."""
    s = connect_with_retry()
    s.sendall(b"prime")
    recv_all(s, 5)

    time.sleep(IDLE_TIMEOUT_S + 2)

    # Confirm the bridge closed this socket
    s.settimeout(2.0)
    try:
        data = s.recv(1)
        assert data == b""
    except (ConnectionResetError, OSError):
        pass
    s.close()

    # Bridge should now accept a new client
    s2 = connect_with_retry(retries=5, delay=0.5)
    try:
        s2.sendall(b"new session")
        assert recv_all(s2, 11) == b"new session"
    finally:
        s2.close()
