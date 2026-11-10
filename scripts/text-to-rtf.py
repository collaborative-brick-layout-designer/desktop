#!/usr/bin/env python3
"""text-to-rtf.py <in.txt> <out.rtf>: a plain-text file as a minimal RTF.

The MSI's license page (WixUI's WixUILicenseRtf) shows RTF only; this turns
LICENSE (GPL-3.0) into one at package time, so the text is never copied by
hand.
"""
import sys


def rtf_escape(text: str) -> str:
    out = []
    for ch in text:
        code = ord(ch)
        if ch in "\\{}":
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\par\n")
        elif ch == "\t":
            out.append("\\tab ")
        elif code < 128:
            out.append(ch)
        else:
            # \uN? with a signed 16-bit N, and "?" for readers without Unicode.
            for unit in ch.encode("utf-16-le").hex(" ", 2).split():
                n = int.from_bytes(bytes.fromhex(unit), "little")
                out.append(f"\\u{n - 65536 if n > 32767 else n}?")
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    text = open(sys.argv[1], encoding="utf-8").read().replace("\r\n", "\n")
    with open(sys.argv[2], "w", encoding="ascii", newline="\r\n") as f:
        f.write("{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fnil Segoe UI;}}\n")
        f.write("\\viewkind4\\uc1\\pard\\f0\\fs16 ")
        f.write(rtf_escape(text))
        f.write("\n}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
