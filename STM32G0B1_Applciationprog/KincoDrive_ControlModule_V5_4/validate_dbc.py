#!/usr/bin/env python3
"""
Vector CANdb++ .dbc File Validator for KincoDrive Control Module

Validates:
1. DBC syntax for BO_ (messages) and SG_ (signals)
2. Extended CAN ID calculations
3. Signal bit layout and overlaps
4. Signal ranges (min <= max)
5. DLC consistency
6. Value table references
7. Comment references
"""

import re
import sys
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional

# Expected message types and their IDs
MESSAGE_TYPES = {
    'Broadcast_Status': 0x600,
    'Broadcast_Currents': 0x601,
    'Broadcast_Temps': 0x602,
    'Broadcast_Fans': 0x603,
    'EEPROM_Config_Response': 0x604,
    'EEPROM_Config_Response2': 0x605,
    'ACK_General': 0x700,
    'Error_Dump': 0x701,
    'Error_Reset_ACK': 0x702,
    'Cmd_Set_LED': 0x060,
    'Cmd_Set_HS_Drive_Power': 0x110,
    'Cmd_Set_HS_Extruder_Power': 0x111,
    'Cmd_Set_HS_Scrubbing_Power': 0x112,
    'Cmd_Set_Fan_PWM': 0x140,
    'Cmd_Set_EEPROM_Config': 0x200,
    'Cmd_Read_EEPROM_Config': 0x201,
    'Cmd_Dump_Errors': 0x703,
    'Cmd_Reset_Error': 0x704,
}

DEVICE_ID = 0x0667


@dataclass
class Signal:
    name: str
    start_bit: int
    bit_length: int
    is_signed: bool  # @1+ = unsigned, @1- = signed
    scale: float
    offset: float
    min_val: float
    max_val: float
    unit: str
    receiver: str


@dataclass
class Message:
    msg_id: int
    name: str
    dlc: int
    sender: str
    signals: List[Signal]


def calculate_expected_can_id(msg_type_id: int) -> int:
    """
    Calculate expected CAN ID using formula:
    CAN ID layout: (msg_type << 16) | 0x0667
    DBC extended flag: actual_29bit_id | 0x80000000
    """
    can_29bit = (msg_type_id << 16) | DEVICE_ID
    dbc_extended = can_29bit | 0x80000000
    return dbc_extended


def parse_signal_line(line: str) -> Optional[Signal]:
    """Parse a SG_ (signal) line from DBC file."""
    # SG_ Signal_Name : start_bit|bit_length@byte_order+/- (scale,offset) [min|max] "unit" receiver
    pattern = r'SG_ (\w+)\s+:\s+(\d+)\|(\d+)@([01])([+-])\s+\(([^,]+),([^)]+)\)\s+\[([^\|]+)\|([^\]]+)\]\s+"([^"]*)"\s+(.+)'
    match = re.match(pattern, line.strip())

    if not match:
        return None

    name, start_bit, bit_length, byte_order, sign, scale, offset, min_val, max_val, unit, receiver = match.groups()

    is_signed = (sign == '-')

    return Signal(
        name=name,
        start_bit=int(start_bit),
        bit_length=int(bit_length),
        is_signed=is_signed,
        scale=float(scale),
        offset=float(offset),
        min_val=float(min_val),
        max_val=float(max_val),
        unit=unit,
        receiver=receiver.strip()
    )


def parse_message_line(line: str) -> Optional[Tuple[int, str, int, str]]:
    """Parse a BO_ (message) line from DBC file."""
    # BO_ msg_id Message_Name: dlc sender
    pattern = r'BO_ (\d+)\s+(\w+):\s+(\d+)\s+(\w+)'
    match = re.match(pattern, line.strip())

    if not match:
        return None

    msg_id, name, dlc, sender = match.groups()
    return int(msg_id), name, int(dlc), sender


def parse_val_line(line: str) -> Optional[Tuple[int, str, Dict[int, str]]]:
    """Parse a VAL_ (value table) line."""
    # VAL_ msg_id signal_name value1 "label1" value2 "label2" ... ;
    pattern = r'VAL_\s+(\d+)\s+(\w+)\s+(.+)\s*;'
    match = re.match(pattern, line.strip())

    if not match:
        return None

    msg_id = int(match.group(1))
    signal_name = match.group(2)
    values_str = match.group(3)

    # Parse value pairs: number "string" number "string"
    val_pattern = r'(\d+)\s+"([^"]*)"'
    values = {}
    for val_match in re.finditer(val_pattern, values_str):
        values[int(val_match.group(1))] = val_match.group(2)

    return msg_id, signal_name, values


def check_signal_overlap(signals: List[Signal]) -> List[str]:
    """Check if signals overlap in bit space."""
    errors = []
    signal_bits = {}

    for signal in signals:
        start = signal.start_bit
        end = start + signal.bit_length

        # Store bit ranges
        if start not in signal_bits:
            signal_bits[start] = []
        signal_bits[start].append((signal.name, end))

    # Check for overlaps
    for i, sig1 in enumerate(signals):
        for sig2 in signals[i+1:]:
            # Simple overlap check: does range [start1, start1+len1) overlap [start2, start2+len2)?
            start1 = sig1.start_bit
            end1 = start1 + sig1.bit_length
            start2 = sig2.start_bit
            end2 = start2 + sig2.bit_length

            # Check overlap
            if not (end1 <= start2 or end2 <= start1):
                errors.append(
                    f"Signal overlap: {sig1.name} (bits {start1}-{end1}) "
                    f"overlaps {sig2.name} (bits {start2}-{end2})"
                )

    return errors


def validate_dbc_file(filepath: str) -> None:
    """Main validation function."""
    print(f"Validating DBC file: {filepath}\n")
    print("=" * 80)

    findings = {
        'syntax_errors': [],
        'id_errors': [],
        'signal_errors': [],
        'dlc_errors': [],
        'value_table_errors': [],
        'comment_errors': [],
        'warnings': [],
    }

    messages: Dict[int, Message] = {}
    signal_lookup: Dict[Tuple[int, str], Signal] = {}  # (msg_id, signal_name)
    val_table_refs: Dict[Tuple[int, str], Dict[int, str]] = {}  # (msg_id, signal_name) -> values
    comment_msg_ids = set()

    # Read file
    with open(filepath, 'r') as f:
        lines = f.readlines()

    # Parse file
    current_message = None

    for line_num, line in enumerate(lines, 1):
        line = line.rstrip('\n')

        # Parse BO_ lines (messages)
        if line.startswith('BO_'):
            result = parse_message_line(line)
            if result:
                msg_id, name, dlc, sender = result
                current_message = Message(
                    msg_id=msg_id,
                    name=name,
                    dlc=dlc,
                    sender=sender,
                    signals=[]
                )
                messages[msg_id] = current_message
            else:
                findings['syntax_errors'].append(f"Line {line_num}: Invalid BO_ format: {line}")
                current_message = None

        # Parse SG_ lines (signals)
        elif line.strip().startswith('SG_'):
            signal = parse_signal_line(line)
            if signal and current_message:
                current_message.signals.append(signal)
                signal_lookup[(current_message.msg_id, signal.name)] = signal
            elif not signal and current_message:
                findings['syntax_errors'].append(f"Line {line_num}: Invalid SG_ format: {line}")

        # Parse VAL_ lines (value tables)
        elif line.startswith('VAL_'):
            result = parse_val_line(line)
            if result:
                msg_id, signal_name, values = result
                val_table_refs[(msg_id, signal_name)] = values
            else:
                findings['syntax_errors'].append(f"Line {line_num}: Invalid VAL_ format: {line}")

        # Track message IDs with comments
        elif line.startswith('CM_ BO_'):
            match = re.match(r'CM_\s+BO_\s+(\d+)', line)
            if match:
                comment_msg_ids.add(int(match.group(1)))

    # Validation checks
    print("\n1. DBC SYNTAX VALIDATION")
    print("-" * 80)
    if findings['syntax_errors']:
        for error in findings['syntax_errors']:
            print(f"  ERROR: {error}")
    else:
        print("  PASS: All BO_ and SG_ lines parse correctly")

    # Check extended CAN ID calculations
    print("\n2. EXTENDED CAN ID VALIDATION")
    print("-" * 80)
    for msg_id, message in sorted(messages.items()):
        # Reverse the DBC formula to get the message type
        dbc_extended = message.msg_id
        can_29bit = dbc_extended & 0x7FFFFFFF  # Remove extended flag

        # Extract msg_type from formula: (msg_type << 16) | 0x0667
        msg_type = (can_29bit >> 16) & 0xFFFF
        device_part = can_29bit & 0xFFFF

        # Check if device part matches
        if device_part != DEVICE_ID:
            findings['id_errors'].append(
                f"{message.name}: Device ID mismatch. Expected 0x{DEVICE_ID:04X}, "
                f"got 0x{device_part:04X} from CAN ID 0x{message.msg_id:08X}"
            )

        # Check if message type is known
        if message.name in MESSAGE_TYPES:
            expected_type = MESSAGE_TYPES[message.name]
            if msg_type != expected_type:
                findings['id_errors'].append(
                    f"{message.name}: Message type mismatch. Expected 0x{expected_type:04X}, "
                    f"got 0x{msg_type:04X}"
                )
                expected_can_id = calculate_expected_can_id(expected_type)
                findings['id_errors'].append(
                    f"  -> Expected CAN ID: 0x{expected_can_id:08X}, "
                    f"got 0x{message.msg_id:08X}"
                )
            else:
                print(f"  PASS: {message.name:30s} | Type: 0x{msg_type:04X} | "
                      f"CAN ID: 0x{message.msg_id:08X}")
        else:
            findings['warnings'].append(
                f"{message.name}: Unknown message type (not in expected list). "
                f"Message type: 0x{msg_type:04X}"
            )
            print(f"  WARN: {message.name:30s} | Type: 0x{msg_type:04X} | "
                  f"CAN ID: 0x{message.msg_id:08X}")

    if findings['id_errors']:
        print("\n  ERRORS:")
        for error in findings['id_errors']:
            print(f"    {error}")

    # Signal bit layout validation
    print("\n3. SIGNAL BIT LAYOUT VALIDATION")
    print("-" * 80)
    for msg_id, message in sorted(messages.items()):
        # Check for overlaps
        overlap_errors = check_signal_overlap(message.signals)
        findings['signal_errors'].extend(overlap_errors)

        # Check total bits don't exceed DLC * 8
        total_bits = sum(sig.bit_length for sig in message.signals)
        dlc_bits = message.dlc * 8

        if total_bits > dlc_bits:
            findings['signal_errors'].append(
                f"{message.name}: Total signal bits ({total_bits}) exceed DLC*8 ({dlc_bits})"
            )

        if overlap_errors:
            print(f"  {message.name}:")
            for error in overlap_errors:
                print(f"    ERROR: {error}")
        else:
            print(f"  PASS: {message.name:30s} | "
                  f"{len(message.signals):2d} signals | "
                  f"{total_bits:2d}/{dlc_bits} bits used")

    # Signal ranges validation
    print("\n4. SIGNAL RANGE VALIDATION")
    print("-" * 80)
    range_errors = []
    for msg_id, message in sorted(messages.items()):
        for signal in message.signals:
            if signal.min_val > signal.max_val:
                range_errors.append(
                    f"{message.name}.{signal.name}: min ({signal.min_val}) > "
                    f"max ({signal.max_val})"
                )

    findings['signal_errors'].extend(range_errors)

    if range_errors:
        for error in range_errors:
            print(f"  ERROR: {error}")
    else:
        print("  PASS: All signal ranges valid (min <= max)")

    # DLC consistency check
    print("\n5. DLC CONSISTENCY CHECK")
    print("-" * 80)
    dlc_issues = []
    for msg_id, message in sorted(messages.items()):
        # For messages with no signals, DLC can be any value
        if not message.signals:
            if message.dlc == 0:
                print(f"  PASS: {message.name:30s} | DLC: {message.dlc} (empty message)")
            else:
                findings['warnings'].append(
                    f"{message.name}: Has DLC={message.dlc} but no signals (could be DLC=0)"
                )
                print(f"  WARN: {message.name:30s} | DLC: {message.dlc} (no signals)")
        else:
            # Calculate minimum required DLC
            max_bit = max(sig.start_bit + sig.bit_length for sig in message.signals)
            min_dlc = (max_bit + 7) // 8  # Round up to nearest byte

            if message.dlc < min_dlc:
                dlc_issues.append(
                    f"{message.name}: DLC too small. DLC={message.dlc} but "
                    f"signals require {min_dlc} bytes"
                )

            print(f"  {'PASS' if message.dlc >= min_dlc else 'ERROR'}: "
                  f"{message.name:30s} | DLC: {message.dlc} | Required: {min_dlc}")

    findings['dlc_errors'].extend(dlc_issues)

    # Value table validation
    print("\n6. VALUE TABLE VALIDATION")
    print("-" * 80)
    val_errors = []
    for (msg_id, signal_name), values in val_table_refs.items():
        # Check message ID exists
        if msg_id not in messages:
            val_errors.append(
                f"VAL_ references unknown message ID: {msg_id} (signal: {signal_name})"
            )
        else:
            # Check signal exists in message
            message = messages[msg_id]
            signal_exists = any(sig.name == signal_name for sig in message.signals)
            if not signal_exists:
                val_errors.append(
                    f"VAL_ references unknown signal: {message.name}.{signal_name}"
                )

    findings['value_table_errors'].extend(val_errors)

    if val_errors:
        for error in val_errors:
            print(f"  ERROR: {error}")
    else:
        print(f"  PASS: All {len(val_table_refs)} value table entries reference valid signals")

    # Comment validation
    print("\n7. COMMENT VALIDATION")
    print("-" * 80)
    comment_errors = []
    for msg_id in comment_msg_ids:
        if msg_id not in messages:
            comment_errors.append(
                f"Comment references unknown message ID: {msg_id}"
            )

    findings['comment_errors'].extend(comment_errors)

    if comment_errors:
        for error in comment_errors:
            print(f"  ERROR: {error}")
    else:
        print(f"  PASS: All {len(comment_msg_ids)} message comments reference valid messages")

    # Summary
    print("\n" + "=" * 80)
    print("VALIDATION SUMMARY")
    print("=" * 80)

    total_errors = (
        len(findings['syntax_errors']) +
        len(findings['id_errors']) +
        len(findings['signal_errors']) +
        len(findings['dlc_errors']) +
        len(findings['value_table_errors']) +
        len(findings['comment_errors'])
    )

    print(f"\nTotal Messages: {len(messages)}")
    print(f"Total Signals: {sum(len(msg.signals) for msg in messages.values())}")
    print(f"Total Value Tables: {len(val_table_refs)}")
    print(f"Total Comments: {len(comment_msg_ids)}")

    print(f"\nSyntax Errors:        {len(findings['syntax_errors'])}")
    print(f"CAN ID Errors:        {len(findings['id_errors'])}")
    print(f"Signal Errors:        {len(findings['signal_errors'])}")
    print(f"DLC Errors:           {len(findings['dlc_errors'])}")
    print(f"Value Table Errors:   {len(findings['value_table_errors'])}")
    print(f"Comment Errors:       {len(findings['comment_errors'])}")
    print(f"Warnings:             {len(findings['warnings'])}")
    print(f"\nTOTAL ERRORS:         {total_errors}")

    if total_errors == 0 and not findings['warnings']:
        print("\nRESULT: PASS - DBC file is valid")
        return 0
    elif total_errors == 0:
        print(f"\nRESULT: PASS with warnings ({len(findings['warnings'])} warnings)")
        return 0
    else:
        print(f"\nRESULT: FAIL - Found {total_errors} errors")
        return 1


if __name__ == '__main__':
    dbc_file = '/sessions/peaceful-cool-volta/mnt/KincoDrive_ControlModule_V5_4/KincoDrive_ControlModule.dbc'
    sys.exit(validate_dbc_file(dbc_file))
