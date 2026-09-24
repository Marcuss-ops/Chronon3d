# TICKET-DAEMON-IPC-REJECTION-RESILIENCE — typed render rejection must preserve daemon

## Stato

OPEN — a typed render capability rejection can terminate the daemon process
instead of returning one failed job reply.

## Evidenza

On 2026-09-24 a strict Vulkan render reached the text node and logged
`UnsupportedCapability: animated glyph state is not supported by canonical
MTSDF`. The client received `ipc read reply: EOF`; the `chronon3d` systemd unit
restarted the daemon, and subsequent jobs failed to connect to the socket.
The rejection is a job-level result and must not poison the transport or the
daemon runtime.

## Exit condition

- Add a daemon IPC integration test that submits a plan which deterministically
  returns `UnsupportedCapability`.
- Require a complete typed error envelope for that request and a successful
  `STATUS`/health request over the same socket afterward.
- Submit a valid render after the rejection and verify the daemon remains ready
  and completes it.
- Verify unexpected process-fatal failures remain distinguishable from normal
  capability errors; do not turn a rejected strict-native request into a
  software fallback.
