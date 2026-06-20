"""
Shared fixtures and helpers for bridge integration tests.

Requirements:
  - ESP32 running the firmware with UART TX (pin 17) shorted to RX (pin 16).
  - Override the target address:
      ESP32_IP=<ip> ESP32_PORT=<port> pytest tests/ -v

The bridge accepts exactly one TCP client at a time. All tests assume a
single active connection through which data echoes back via UART loopback.
"""

import os
import socket
import time

import pytest

ESP32_IP        = os.environ.get("ESP32_IP", "10.20.32.29")
ESP32_PORT      = int(os.environ.get("ESP32_PORT", "3000"))
RECV_TIMEOUT_S  = 5.0
IDLE_TIMEOUT_S  = 10    # must match bridge idle_timeout in bridge.c (default 10 s)
BRIDGE_BUFFER_SZ = 1024 # must match BRIDGE_BUFFER_SZ in bridge.c


def recv_all(sock, length, timeout=RECV_TIMEOUT_S):
    """Receive exactly *length* bytes, raising TimeoutError or ConnectionError."""
    data = b""
    deadline = time.monotonic() + timeout
    while len(data) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(
                f"recv_all timed out after {timeout}s: "
                f"got {len(data)}/{length} bytes"
            )
        sock.settimeout(remaining)
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise ConnectionError(
                f"Connection closed early: got {len(data)}/{length} bytes"
            )
        data += chunk
    return data


def connect_with_retry(ip=ESP32_IP, port=ESP32_PORT,
                       retries=5, delay=0.3,
                       timeout=RECV_TIMEOUT_S):
    """Connect to the bridge, retrying on transient failures.

    The bridge needs a tick cycle (~10 ms) to return to WAIT state after a
    previous client disconnects, so a single retry with a short delay is
    usually enough; more retries protect against slow WiFi round-trips.
    """
    last_exc = None
    for attempt in range(retries):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(timeout)
            s.connect((ip, port))
            return s
        except OSError as exc:
            last_exc = exc
            if attempt < retries - 1:
                time.sleep(delay)
    raise ConnectionRefusedError(
        f"Could not connect to {ip}:{port} after {retries} attempts"
    ) from last_exc


@pytest.fixture
def sock():
    """Fresh TCP connection to the bridge, closed after each test."""
    s = connect_with_retry()
    yield s
    try:
        s.close()
    except Exception:
        pass
