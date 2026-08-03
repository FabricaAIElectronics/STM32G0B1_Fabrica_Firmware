"""Hardware-in-the-loop verification of a board's CAN behaviour.

Three properties, in the order they should be checked:

  1. UNSOLICITED  every message the DBC says the board sends must appear
                  without the host asking for anything. Telemetry that only
                  arrives after a poll is a different design, and a host
                  waiting for a periodic frame would hang forever.

  2. CAUSAL       a command must visibly change the telemetry it governs. Set a
                  fan duty, watch the fan broadcast follow. This is what proves
                  the receive path, the application logic and the transmit path
                  are actually connected, rather than each merely being present.

  3. RESET        the FF 00 trigger must restart the board into its bootloader:
                  application broadcasts stop, and the bootloader answers XCP on
                  the board's TX id.

All three transmit on the bus. Nothing here runs without an explicit opt-in from
the caller, because the bench bus is shared with other equipment.

The bus is injected, so every check is unit-testable over python-can's virtual
backend with a scripted responder standing in for a board.
"""
from __future__ import annotations

import time
from dataclasses import dataclass, field

from . import canbus, canflash

PASS, FAIL, SKIP = "pass", "fail", "skip"


@dataclass
class VerifyResult:
    name: str
    status: str
    detail: str
    evidence: list = field(default_factory=list)


#: Per board: the command to send, and the broadcast that must change.
#: payload_for(value) builds the command; extract(signals) pulls the value back
#: out of the telemetry so the two can be compared.
#: Each pair must be a genuine ECHO: a command field the board reflects back in
#: telemetry. That is what makes equality a valid assertion.
#:
#: Two things that look like echoes and are not:
#:   * a MEASURED value. KincoDrive's Bcast_Fans (0x123) is fan TACHOMETER
#:     percent; a real fan takes time to spin up and never reports exactly the
#:     commanded duty.
#:   * a STORED value. KincoDrive's Bcast_Config_A (0x126) packs
#:     EEPROM_GetCachedConfig(), so a RAM-only Cmd_OC_Threshold correctly does
#:     not move it until a CMD_EEPROM save.
#:
#: raw_index is the payload byte to compare when no DBC is loaded.
STIMULUS = {
    "powerstage": {
        "cmd_id": 0x140, "cmd_name": "CMD_FAN",
        "bcast_id": 0x153, "bcast_name": "BCAST_FAN",
        "payload_for": lambda v: bytes([1, v]),      # mode=1 (on), duty=v
        "signal": "Fan_Duty_State", "raw_index": 1,
        "values": (30, 70),
    },
    # KincoDrive has NO non-actuating echo, so its causal check is opt-in.
    #
    # Cmd_OC_Threshold -> Bcast_Config_A looks like an echo and is not:
    # send_bcast_config_a() packs EEPROM_GetCachedConfig(), while
    # Cmd_OC_Threshold sets the live protection threshold and is documented as
    # RAM-only until a CMD_EEPROM save. Pairing them produced a confident
    # "the command is not reaching the telemetry" against a board doing exactly
    # what it was designed to do.
    #
    # The only genuine live-state echo this board has is Cmd_HS_Power ->
    # Bcast_GPIO, and that physically switches the high-side power rails. That
    # is a real-world side effect, so it requires --allow-actuate rather than
    # riding along with the other checks.
    "kincodrive": {
        "cmd_id": 0x110, "cmd_name": "Cmd_HS_Power",
        "bcast_id": 0x124, "bcast_name": "Bcast_GPIO",
        "payload_for": lambda v: bytes([v]),      # bitmask of rail enables
        "signal": None, "raw_index": 0,
        # CAN_Handler.c: 0x110 byte[0] is bit0=Drive bit1=Extruder
        # bit2=Scrubbing bit3=12V-Buck, and 0x124 byte[0] echoes exactly those
        # four HS_IsEnabled() bits. Only the Scrubbing rail is toggled, and the
        # sequence ends on 0x0F so the board is left as it was found.
        "values": (0x0B, 0x0F),   # scrubbing rail off, then all four back on
        "actuates": "switches the high-side power rails on this board",
    },
    # The knob echoes the commanded common-line mask back in KNOBSTATE within
    # one 500 ms GPIO update tick, so this is a true echo. It only toggles the
    # encoder common lines - nothing moves.
    "knob": {
        "cmd_id": 0x665, "cmd_name": "KNOBCOMMAND",
        "bcast_id": 0x661, "bcast_name": "KNOBSTATE",
        "payload_for": lambda v: bytes(7) + bytes([v]),   # mask lives in byte 7
        "signal": "Com_State", "raw_index": 6,
        "values": (0x05, 0x07),        # enc0+enc2, then all three back on
    },
    "leddriver": {
        "cmd_id": 0x170, "cmd_name": "CMD_LIGHTSET",
        "bcast_id": 0x179, "bcast_name": "BCAST_LIGHTSTATUS",
        "payload_for": lambda v: bytes([v, v, v]),
        "signal": "PWM_Ch0_State", "raw_index": 0,
        "values": (30, 70),
        # BCAST_LIGHTSTATUS reports the APPLIED duty, and the driver refuses to
        # drive the LEDs while an undervoltage shutdown is in force. On a bench
        # where only logic power is connected the board sits in STATE_ERROR
        # forever, so CMD_LIGHTSET is ignored by design - asserting causality
        # there would report a bug against correct protective behaviour.
        "requires": {
            "bcast_id": 0x17A, "bcast_name": "BCAST_DEVSTATUS",
            "raw_index": 0, "not_value": 3, "state_name": "STATE_ERROR",
            "reason": "the board is in STATE_ERROR (undervoltage): it sheds "
                      "the LED outputs by design and ignores CMD_LIGHTSET "
                      "until the 24V and 17.5V rails recover",
        },
    },
}


def _drain(bus, seconds: float, monitor: canbus.Monitor) -> None:
    """Pump everything the bus offers for a fixed wall-clock window."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        msg = bus.recv(timeout=max(0.0, deadline - time.time()))
        if msg is not None:
            monitor.observe(msg.arbitration_id, bytes(msg.data), msg.timestamp)


def check_unsolicited(bus, board, db, expected_ids: set[int],
                      seconds: float = 3.0,
                      min_rate_hz: float = 0.5) -> VerifyResult:
    """Property 1: the board broadcasts without being asked."""
    monitor = canbus.Monitor(db)
    _drain(bus, seconds, monitor)

    seen = {f.arb_id for f in monitor.snapshot()}
    missing = sorted(expected_ids - seen)
    counts = monitor.counts()
    slow = sorted(i for i in (expected_ids & seen)
                  if (monitor.rate_hz(i) or 0) < min_rate_hz and counts[i] < 2)

    evidence = [{"id": hex(i), "count": counts.get(i, 0),
                 "rate_hz": round(monitor.rate_hz(i) or 0.0, 1)}
                for i in sorted(expected_ids)]

    if not seen:
        return VerifyResult(
            "unsolicited", FAIL,
            f"no CAN traffic at all in {seconds:.0f}s - board powered? "
            f"bus terminated? correct interface?", evidence)
    if missing:
        return VerifyResult(
            "unsolicited", FAIL,
            f"{len(missing)} expected broadcast(s) never arrived: "
            f"{', '.join(hex(i) for i in missing)}", evidence)
    if slow:
        return VerifyResult(
            "unsolicited", FAIL,
            f"seen only once, so not demonstrably periodic: "
            f"{', '.join(hex(i) for i in slow)}", evidence)
    return VerifyResult(
        "unsolicited", PASS,
        f"all {len(expected_ids)} broadcasts arrived unsolicited", evidence)


def _observe_byte(bus, arb_id: int, index: int, timeout: float = 3.0):
    """First `index` byte seen on `arb_id`, or None if it never arrives.

    Passive: sends nothing. Used for preconditions, where transmitting to
    establish the state under test would defeat the purpose.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = bus.recv(timeout=max(0.0, deadline - time.time()))
        if msg is None or msg.arbitration_id != arb_id:
            continue
        raw = bytes(msg.data)
        if len(raw) > index:
            return raw[index]
    return None


def check_command_changes_telemetry(bus, board, db, timeout: float = 3.0,
                                    allow_actuate: bool = False
                                    ) -> VerifyResult:
    """Property 2: a command visibly changes the telemetry it governs.

    The command/telemetry pair must be a genuine ECHO - a field the board
    reflects back - or equality is not a valid assertion. Two ways that goes
    wrong, both learned the hard way:

      * a MEASURED quantity is not an echo. A fan tachometer never reports
        exactly the commanded duty.
      * a CONFIGURATION echo may report stored settings rather than live ones.
        KincoDrive's Bcast_Config_A packs the EEPROM cache, so a RAM-only
        Cmd_OC_Threshold correctly does not move it.
    """
    spec = STIMULUS.get(board.id)
    if spec is None:
        return VerifyResult("causal", SKIP,
                            f"no stimulus defined for {board.id}", [])
    if spec.get("actuates") and not allow_actuate:
        return VerifyResult(
            "causal", SKIP,
            f"{board.id}'s only genuine echo is {spec['cmd_name']}, which "
            f"{spec['actuates']}. Pass --allow-actuate to run it.", [])

    # A causal claim needs the telemetry to FOLLOW the command, which cannot be
    # observed unless the command actually changes. kincodrive shipped with
    # values (0x0F, 0x0F) and reported a confident
    #   PASS causal  Cmd_HS_Power changed Bcast_GPIO: 15->15, 15->15
    # against a board that was already sitting at 0x0F - it would have passed
    # identically with the CAN transmit pair cut. A green result that cannot go
    # red is worse than the SKIP it replaced, so a degenerate stimulus is a
    # failure of the test definition and is reported as one.
    values = tuple(spec["values"])
    if len(set(values)) < 2:
        return VerifyResult(
            "causal", FAIL,
            f"stimulus for {board.id} commands {values}: a causal check needs "
            f"at least two distinct values, otherwise it passes on a board "
            f"that ignores the command entirely", [])

    # A precondition the board must be in before causality is even meaningful.
    # Checked by listening, never by commanding: the point is to describe the
    # bench honestly, not to change it into a testable state.
    need = spec.get("requires")
    if need is not None:
        state = _observe_byte(bus, need["bcast_id"], need["raw_index"],
                              timeout=timeout)
        if state is None:
            return VerifyResult(
                "causal", SKIP,
                f"{need['bcast_name']} (0x{need['bcast_id']:03X}) never "
                f"arrived, so the board's state could not be established", [])
        if state == need["not_value"]:
            return VerifyResult(
                "causal", SKIP,
                f"{need['reason']}. Power the rails and re-run to check "
                f"causality.",
                [{"state": state, "state_name": need["state_name"]}])

    observations = []
    for value in values:
        canbus.send_frame(bus, spec["cmd_id"], spec["payload_for"](value))
        deadline = time.time() + timeout
        got = None
        while time.time() < deadline:
            msg = bus.recv(timeout=max(0.0, deadline - time.time()))
            if msg is None or msg.arbitration_id != spec["bcast_id"]:
                continue
            frame = canbus.decode_frame(db, msg.arbitration_id,
                                        bytes(msg.data), msg.timestamp)
            # Prefer the decoded signal; fall back to the raw byte when there is
            # no DBC or the signal is absent, so this works on a bench with only
            # a board and a cable.
            got = frame.signals.get(spec["signal"]) if frame.signals else None
            if got is None:
                raw = bytes(msg.data)
                idx = spec["raw_index"]
                got = raw[idx] if len(raw) > idx else None
            if got == value:
                break
        observations.append({"commanded": value, "reported": got})

    if any(o["reported"] is None for o in observations):
        return VerifyResult(
            "causal", FAIL,
            f"{spec['bcast_name']} (0x{spec['bcast_id']:03X}) never arrived "
            f"after {spec['cmd_name']}", observations)
    if all(o["reported"] == o["commanded"] for o in observations):
        return VerifyResult(
            "causal", PASS,
            f"{spec['cmd_name']} changed {spec['bcast_name']}: "
            + ", ".join(f"{o['commanded']}->{o['reported']}" for o in observations),
            observations)
    if len({o["reported"] for o in observations}) == 1:
        return VerifyResult(
            "causal", FAIL,
            f"{spec['bcast_name']} did not change: reported "
            f"{observations[0]['reported']} for both commanded values - the "
            f"command is not reaching the telemetry", observations)
    return VerifyResult(
        "causal", FAIL,
        f"{spec['bcast_name']} changed but not to the commanded value",
        observations)


def _max_gap(bus, seconds: float) -> tuple[float, int]:
    """Largest silence between consecutive frames in a window, and the count.

    `last` is seeded with the window's start rather than the first frame. If a
    board goes quiet the instant the trigger lands, the silence sits between the
    start of the window and the first frame after it - and starting from the
    first frame would miss exactly the gap this exists to detect. It only
    happened to work on hardware because one broadcast slipped out before the
    reset took hold.
    """
    start = time.time()
    end = start + seconds
    last = start
    biggest = 0.0
    seen = 0
    while time.time() < end:
        msg = bus.recv(timeout=max(0.0, end - time.time()))
        now = time.time()
        if msg is None:
            continue
        seen += 1
        biggest = max(biggest, now - last)
        last = now
    return biggest, seen


def check_reset_enters_bootloader(bus, board, quiet_for: float = 4.0,
                                  gap_factor: float = 4.0) -> VerifyResult:
    """Property 3: FF 00 makes the board restart through its bootloader.

    The observable is a GAP in the broadcasts, not permanent silence.

    An earlier version asserted the application stopped and stayed stopped.
    That is wrong for OpenBLT: the bootloader only remains resident if a host
    connects during its backdoor window, which is a few hundred milliseconds.
    With nothing connecting, it starts the application again, so a correctly
    resetting board is broadcasting again within about a second - and the check
    called that a failure on hardware that was behaving perfectly.

    What a reset really looks like from the bus is a pause: the application
    stops, the MCU reboots, the bootloader waits out its backdoor, then the
    application starts and resumes. That pause is far longer than the normal
    broadcast interval, so comparing the largest inter-frame gap before and
    after the trigger detects it without assuming the bootloader stays put.
    """
    baseline_gap, baseline_seen = _max_gap(bus, 1.5)
    if baseline_seen < 2:
        return VerifyResult(
            "reset", SKIP,
            "board was not broadcasting before the reset, so a restart cannot "
            "be observed", [])

    canbus.send_reset(bus, board.blt_rx)
    after_gap, after_seen = _max_gap(bus, quiet_for)

    evidence = [{"baseline_max_gap_s": round(baseline_gap, 3),
                 "baseline_frames": baseline_seen,
                 "after_max_gap_s": round(after_gap, 3),
                 "after_frames": after_seen,
                 "threshold_s": round(baseline_gap * gap_factor, 3)}]

    if after_seen == 0:
        return VerifyResult(
            "reset", PASS,
            f"application went silent after FF 00 on 0x{board.blt_rx:03X} and "
            f"had not returned after {quiet_for:.0f}s - board is sitting in its "
            f"bootloader", evidence)
    if after_gap >= baseline_gap * gap_factor:
        return VerifyResult(
            "reset", PASS,
            f"broadcasts paused for {after_gap:.2f}s after FF 00 (normal gap "
            f"{baseline_gap:.3f}s) - the board restarted through its bootloader "
            f"and the application came back", evidence)
    return VerifyResult(
        "reset", FAIL,
        f"no restart seen: largest gap after the trigger was {after_gap:.3f}s, "
        f"barely different from the normal {baseline_gap:.3f}s between frames. "
        f"The board appears to have ignored FF 00 on 0x{board.blt_rx:03X}",
        evidence)


def run_all(bus, board, db, expected_ids: set[int], *,
            include_reset: bool = False, seconds: float = 3.0,
            allow_actuate: bool = False) -> list[VerifyResult]:
    """Run the checks in order. The reset check is last: it stops the board."""
    results = [check_unsolicited(bus, board, db, expected_ids, seconds)]
    results.append(check_command_changes_telemetry(
        bus, board, db, allow_actuate=allow_actuate))
    if include_reset:
        results.append(check_reset_enters_bootloader(bus, board))
    else:
        results.append(VerifyResult(
            "reset", SKIP,
            "not run - pass --include-reset. It restarts the board into its "
            "bootloader and stops the application", []))
    return results


def expected_broadcast_ids(db, blt_tx: int) -> set[int]:
    """Ids the DBC says the board sends, excluding the bootloader's own TX."""
    if db is None:
        return set()
    out = set()
    for msg in db.messages:
        senders = set(msg.senders or ())
        if senders - {"Master", "Host", "Tester"} and msg.frame_id != blt_tx:
            out.add(msg.frame_id)
    return out
