#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Labeled disassembler for the SDI103J ROM (docs/VC1541/SDI103J_ED3DC6CD.ROM),
# grounded in REAL execution, not pure static guessing.
#
# Why this exists instead of trusting ghidra's/skoolkit's own dumps
# (docs/VC1541/SDI103J_ED3DC6CD_ghidra_disasm.txt, skoolkit/*.skool), or even
# a fresh from-scratch static z80dis sweep: this ROM has embedded string/data
# tables (confirmed: $31e3-$3338 message table + banner, $35aa-ish command
# keyword table) that any purely-static linear decoder can drift through --
# once it misjudges one instruction's length inside a data blob, every
# following address is wrong until the next lucky resync. Ghidra and skoolkit
# both hit this; an early version of this script did too.
#
# The fix: don't trust static decode where we have something better.
# tools/instrtrace_decode.py-style INSTRTRACE logs from REAL JASE runs give
# the TRUE instruction boundary and TRUE bytes for every address the Z80
# actually fetched an opcode at -- no ambiguity possible, because that's
# where the real CPU really stopped. This script loads one or more such
# logs, builds a ground-truth {address: (raw_bytes, mnemonic)} map from
# them, and uses THAT for every address it covers. Only addresses never
# seen executing (a REAL run inevitably won't cover 100% of a ROM -- not
# every byte is reachable code, and some reachable code just wasn't
# exercised this session) fall back to a static z80dis decode, clearly
# marked as such so nobody mistakes it for a confirmed fact.
#
# Usage:
#   python3 tools/sdi_disasm.py <instrtrace.log> [more logs...]
# -----------------------------------------------------------------------------
import re
import sys

import z80dis.z80 as z80

ROM_PATH = "docs/VC1541/SDI103J_ED3DC6CD.ROM"
BASE = 0x3000

TRACE_RE = re.compile(
    r"pc=(?P<pc>[0-9a-f]+) bytes=(?P<bytes>[0-9a-f]{8})"
)

# Confirmed this session via live BREAK/STEP/REGS/MEM tracing, or carried
# over from docs/VC1541/VC1541.md's own earlier confirmed-by-trace work
# (not from reading the static disassembly and inferring).
SYMBOLS = {
    0x31a9: "PRINT_MSG_SKIP_LOOP",       # BIT 7,(HL)/INC HL/JR Z skip pattern, HL base = MSG_TABLE
    0x31b5: "MSG_SKIP_DJNZ",             # CORRECTED 2026-10-28 (was wrongly "DOS_Display_Error" -- that name
                                          # implied a distinct entry point, it isn't one). This is just the DJNZ
                                          # inside PRINT_MSG_SKIP_LOOP's own body ($31a9 is the real entry): XOR A/
                                          # LD HL,$31e3/CP B/JR Z,$31b7/[loop: BIT 7,(HL)/INC HL/JR Z]/DJNZ here --
                                          # walks HL forward exactly B messages (B = 0-based index set by the
                                          # caller: FILE NOT FOUND=0, DEVICE NOT PRESENT=1, SEARCHING FOR=2, ...),
                                          # then falls into $31b7 to print from HL. Confirmed live: BREAK 31a9 +
                                          # loop-until-B==1 landed here with HL=$31f2 (DEVICE NOT PRESENT's exact
                                          # address). The real generic "please display error N" entry point is
                                          # ERR_DISPATCH ($3339): caller sets B=index, CALLs ERR_DISPATCH, which
                                          # CALLs PRINT_MSG_SKIP_LOOP ($31a9) with B intact.
    0x3025: "ATN_PRESENCE_DEBOUNCE",     # confirmed live 2026-10-28: read Port C, short delay, re-read, compare,
                                          # loop until 2 consecutive reads match, SLA A/RET hands caller a stable
                                          # bus-line bit via carry (bit 7 of Port C = DAT, see SDI_8255.cpp). Only
                                          # confirmed caller: $3142, inside IEC_RECEIVE_BYTE's own CLK-release wait.
    0x3036: "SET_DEVICE_NOT_PRESENT_FLAG",  # confirmed live/passive-read 2026-10-28 (CPU paused mid-ROM at the
                                          # time, ROMCS confirmed asserted): LD A,$80/LD BC,$033e/LD HL,$5c92/
                                          # OR (HL)/LD (HL),A -- sets bit 7 of the status byte at $5c92. Reached
                                          # via $30d4's own "IN A,($DF)/OR A/JP M,$3036" (DAT line, Port C bit 7,
                                          # reading as released when $30d4 -- DOS_Send_Byte_IEC -- samples it
                                          # mid-transfer). $5c92 bit 7 is what $347e (inside
                                          # IEC_OPEN_SEND_SHARED) later tests via JP M to dispatch "DEVICE NOT
                                          # PRESENT" (B=1, see MSG_B1_SET_AND_FALL below). This is a DIFFERENT
                                          # check than IecListenerState::enterAtnPresenceAck (confirmed separately
                                          # this session, 160/160 correct) -- $30d4 fires during actual bit/byte
                                          # transmission, not the one-shot ATN presence-ack, so the real-emulator
                                          # root cause (if any) is more likely in IecListenerState's bit-receive
                                          # substates (WaitReadyToSend/EoiAckPulse/Receiving/AckHold), not
                                          # AtnPresenceAck. NOT yet found: the exact JASE-side condition that
                                          # leaves DAT looking released at the moment $30d4 samples it.
    0x3331: "MSG_B1_SET_AND_FALL",       # confirmed live 2026-10-28 (live INSTRTRACE PC-sequence + passive read):
                                          # LD B,1 (sets the DEVICE NOT PRESENT message-table index) then falls
                                          # through into ERR_DISPATCH ($3339). Reached from $347e's "JP M,$3331"
                                          # inside IEC_OPEN_SEND_SHARED when ($5c92) bit 7 is set.
    0x3060: "IEC_UNLISTEN",              # VC1541.md's confirmed LOAD call chain
    0x3075: "IEC_SEND_DEVNUM",           # VC1541.md
    0x3078: "IEC_LISTEN_DEVNUM",         # VC1541.md
    0x30d4: "DOS_Send_Byte_IEC",         # confirmed live/passive-read 2026-10-28: generic single-bit/byte IEC
                                          # send routine (ROM procedure address list, independently re-confirmed
                                          # this session). Contains its own "IN A,($DF)/OR A/JP M,$3036" DAT-line
                                          # check mid-transfer -- see $3036 above, this is the real trigger site
                                          # for DEVICE NOT PRESENT during a send, not just at ATN time.
    0x3138: "IEC_RECEIVE_BYTE",          # earlier-session confirmed trace (project memory)
    0x3339: "ERR_DISPATCH",              # confirmed live: PUSH BC/CALL $3e76/POP BC/CALL PRINT_MSG_SKIP_LOOP
    0x3350: "IEC_SEND_SECONDARY_RELEASE_ATN",  # VC1541.md
    0x345e: "IEC_OPEN_SEND_SHARED",      # VC1541.md -- LISTEN+devnum, secondary, char-send loop, UNLISTEN
    0x3512: "CATALOG_REOPEN_AND_PEEK7",  # confirmed live: re-opens ch0 "$", reads 7 bytes into IX buf
    0x3515: "CATALOG_OPEN_CH0_ENTRY",    # RESOLVED 2026-10-28 (was flagged as an unresolved contradiction
                                          # earlier the same session -- see git history on this comment if curious
                                          # how it was wrong). An initial live MEM peek at this address, mid-
                                          # breakpoint on $345e (return addr $351d), read "3e 60 32 9d 5c cd 5e
                                          # 34..." = LD A,$60/LD ($5c9d),A/CALL IEC_OPEN_SEND_SHARED -- opens ch0
                                          # secondary $60 (Open), the real *C/*LOAD-turnaround path. This directly
                                          # CONTRADICTED two independent INSTRTRACE "ground truth" captures (this
                                          # file's own pre-existing entry, and a fresh regeneration from this same
                                          # session's trace_final.log), both of which agreed on a different
                                          # reading ("23 77 23 77 e1 c9" = INC HL/LD(HL),A/INC HL/LD(HL),A/POP HL/
                                          # RET) for the same bytes. Settled with a direct live BREAK 3515 + STEP
                                          # sequence: PC moved 3515->3517->351a->345e exactly as the live-peek
                                          # decode predicts, AF/SP changed consistently (SP -2 confirming the
                                          # CALL), conclusively confirming the live-peek reading was right and
                                          # BOTH INSTRTRACE ground-truth sources were wrong at this address. Why
                                          # INSTRTRACE itself produced a wrong, mutually-agreeing answer here
                                          # twice is still not understood -- a real caution about trusting even
                                          # repeated ">> confirmed" entries without a live spot-check when
                                          # something about the surrounding context looks inconsistent.
    0x354d: "CATALOG_PEEK7_STORE_BYTE",  # the LD (HL),A inside CATALOG_REOPEN_AND_PEEK7's DJNZ loop
    0x3552: "CATALOG_REOPEN_RET",
    0x359e: "TYPE_CHECK_CP",             # confirmed live: LD A,(IX-7)/CP (IX+0)/RET Z else WRONG_FILE_TYPE
    0x35a5: "TYPE_CHECK_FAIL_B9",        # LD B,9 -- message index for WRONG FILE TYPE
    0x3764: "TYPE_CHECK_FIELD_ZERO",     # confirmed live: LD (IX),0 -- generic record-init, NOT type-related
    0x3768: "BLOCKFREE_SBC_CALC",        # confirmed live: free-block subtract/store into same record
    0x3791: "RECORD_PTR_ADVANCE_STORE",  # LD (0x5c5f),IX after ADD IX,BC(7) -- confirmed live
    0x3795: "CALL_CATALOG_REOPEN",       # the actual CALL 0x3512 call site
    0x3798: "LOAD_A_FE_BEFORE_TRAMP",
    0x379a: "JP_TRAMP_3e03",
    0x379d: "CALL_TYPE_CHECK",           # the actual CALL 0x359e call site, reached via $3e03 trampoline
    0x3996: "LOAD_OPEN_SHARED_CONT",     # VC1541.md -- append CR, 8255->$98, (5c9d)=$6f, falls into $345e
    0x3c52: "SEND_I_AND_UIMINUS",        # confirmed live 2026-10-28 via BREAK 345e + return-address capture
                                          # (return addrs $3c5a, $3c5d): LD A,$49('I')/LD (0x5c92),A/
                                          # CALL PARSE_QUOTED_FILENAME (sends "I"+CR)/CALL UIMINUS_STRING_SETUP
                                          # (sends "UI-"+CR)/EI/RET. Always sends BOTH as one inseparable pair,
                                          # right after the boot-banner print loop -- this is RANDOMIZE USR 16000's
                                          # own activation sequence, NOT a per-retry resend of "I" alone (the
                                          # bare-"I"-repeating retry storm must come from a still-unidentified
                                          # different call site -- see project memory).
    0x3c56: "CMD_LOAD_ENTRY",            # VC1541.md's confirmed command-table dispatch
    0x3e03: "TRAMP_CALL1601_TO_379D",    # CALL $1601 / JP $379d -- one of a family of CALL+JP trampolines at $3e00-$3e21
    0x3e2b: "PRINT_CHAR_RST10",          # confirmed live/byte-exact 2026-10-28: PUSH AF/EI/RST $10/LD A,(HL)/
                                          # POP AF/RET -- print-one-character-from-HL helper, the hotspot found
                                          # via INSTRTRACE during the DEVICE NOT PRESENT flood (268 hits in one
                                          # sample -- it's reprinting the message, not silently looping).
    0x3e76: "CALL1601_A_FE_RET",         # LD A,0xFE / CALL $1601 / RET -- same host-ROM call as $3e03's trampoline
    0x3f33: "PARSE_QUOTED_FILENAME",     # VC1541.md -- RST $30 BC-SPACES convention
    0x3f47: "UIMINUS_STRING_SETUP",      # confirmed live/byte-exact 2026-10-28: LD DE,$3f50/LD BC,4/JP $3ebd --
                                          # $3f50 itself is literal data, bytes "55 49 2D 0D" = "UI-\r" (4 bytes,
                                          # matching BC=4 here), NOT real code despite how z80dis renders it.
    0x1601: "HOST_ROM_1601",             # standard system ROM, always present below $3000; clobbers BC/DE/HL/AF,
                                          # confirmed independent of filename, NOT independently decoded this session
}

# Confirmed byte-exact by direct parse (bit7-terminator convention),
# cross-checked against TYPE_CHECK_FAIL_B9's live B=9 value landing on
# "WRONG FILE TYPE" at index 9. See project memory
# project_jase_sdi_i8255_rebuild.md for the parse.
DATA_REGIONS = [
    (0x31e3, 0x3339, "MSG_TABLE_AND_BANNER", "CONFIRMED byte-exact"),
    # An earlier version of this script also excluded $35aa-$3626 as a
    # second, "approximate" data table (VC1541.md's command-keyword
    # table). Removed: ground-truth INSTRTRACE data shows REAL confirmed
    # code inside that guessed range (e.g. $35bf: LD HL,(0x5c65) actually
    # executes) -- the guessed boundary was wrong and was silently
    # suppressing real confirmed facts by rendering them as DEFM text.
    # Whatever of that region isn't covered by ground truth just falls
    # through to the static decoder below, correctly flagged as unconfirmed.
]


def load_rom():
    with open(ROM_PATH, "rb") as f:
        return f.read()


def load_trace_map(paths):
    gt = {}
    for p in paths:
        with open(p, errors="replace") as f:
            for line in f:
                m = TRACE_RE.search(line)
                if not m:
                    continue
                pc = int(m.group("pc"), 16)
                raw = bytes.fromhex(m.group("bytes"))
                gt[pc] = raw  # last-seen wins; bytes at a given PC don't change across runs
    return gt


def in_data_region(addr):
    for start, end, label, conf in DATA_REGIONS:
        if start <= addr < end:
            return (start, end, label, conf)
    return None


def render_text_region(data, start, end):
    out = []
    pos = start
    cur = bytearray()
    while pos < end:
        b = data[pos - BASE]
        cur.append(b & 0x7f)
        if b & 0x80 and (b & 0x7f) != 0:
            text = bytes(cur).decode("ascii", errors="replace")
            out.append(f"  ${pos-len(cur)+1:04x}: DEFM {text!r} (last char flagged, 0x{b:02x})")
            cur = bytearray()
        pos += 1
    if cur:
        out.append(f"  ${pos-len(cur):04x}: DEFB {list(cur)!r} (trailing, unterminated)")
    return out


def substitute_symbols(mnem):
    for sym_addr, sym_name in SYMBOLS.items():
        needle = f"0x{sym_addr:04x}"
        if needle in mnem:
            mnem = mnem.replace(needle, sym_name)
    return mnem


def main():
    trace_paths = sys.argv[1:]
    if not trace_paths:
        sys.exit("usage: sdi_disasm.py <instrtrace.log> [more logs...]")

    data = load_rom()
    gt = load_trace_map(trace_paths)
    end_addr = BASE + len(data)

    lines = []
    lines.append(f"; SDI103J_ED3DC6CD.ROM -- labeled disassembly, {len(data)} bytes")
    lines.append(f"; Ground truth from {len(trace_paths)} INSTRTRACE log(s): {len(gt)} confirmed addresses")
    lines.append("; '>>' = confirmed by real JASE execution this session (ground truth).")
    lines.append("; '..' = statically decoded only (z80dis), NOT independently confirmed --")
    lines.append(";        may be wrong if this is actually data the Z80 never fetched here.")
    lines.append("")

    addr = BASE
    while addr < end_addr:
        dr = in_data_region(addr)
        if dr:
            start, end, label, conf = dr
            lines.append(f"; --- DATA REGION {label} [${start:04x}-${end-1:04x}] ({conf}) ---")
            lines.extend(render_text_region(data, start, end))
            lines.append(f"; --- end {label} ---")
            lines.append("")
            addr = end
            continue

        if addr in SYMBOLS:
            lines.append(f"{SYMBOLS[addr]}:")

        off = addr - BASE

        if addr in gt:
            raw_full = gt[addr]
            try:
                dec = z80.decode(raw_full, addr)
                length = dec.len if dec else 1
                raw = raw_full[:length]
                mnem = z80.disasm(dec) if dec else "???"
            except Exception:
                length = 1
                raw = raw_full[:1]
                mnem = "???"
            mnem = substitute_symbols(mnem)
            lines.append(f"  ${addr:04x}: {raw.hex():<8} {mnem:<28} >> confirmed")
        else:
            # z80dis.decode(buf, addr) reads buf starting at buf[0] -- it
            # does NOT index into a larger buffer by addr, addr is only
            # used to label the result (e.g. resolve relative jump
            # targets). Must pass a short slice starting exactly at this
            # instruction, same convention tools/instrtrace_decode.py
            # already uses -- an earlier version of this script passed the
            # whole ROM + an offset here, which silently decoded ROM byte
            # 0 (IN A,(0xDF)) every single time instead of the real bytes.
            chunk = data[off:off + 4]
            try:
                dec = z80.decode(chunk, addr)
                length = dec.len if dec else 1
                raw = chunk[:length]
                mnem = z80.disasm(dec) if dec else "???"
            except Exception as e:
                lines.append(f"  ${addr:04x}: <decode error: {e}>")
                addr += 1
                continue
            mnem = substitute_symbols(mnem)
            lines.append(f"  ${addr:04x}: {raw.hex():<8} {mnem:<28} .. static only")

        addr += length

    print("\n".join(lines))


if __name__ == "__main__":
    main()
