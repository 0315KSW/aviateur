#!/usr/bin/env python3
"""Summarise an Aviateur console log (upstream v0.3.3 or lowlat patch).

Counts the events we used to judge each receive test:
  - SESSION lines            : WFB-ng session key received (decrypt OK)
  - Generated SDP            : RTP reached the player (codec detected)
  - Starting decoder         : SPS/PPS + keyframe received, decoding started
  - RTP packets lost: N      : gaps in the RTP sequence after WFB-ng FEC
  - Could not find ref       : decoder lost a reference frame (picture damage)
  - PPS id out of range      : decoder got slices without parameter sets
  - Unable to read rx secret key / decrypt errors

Usage: python3 analyze_aviateur_log.py <log.txt>
"""
import re
import sys
from collections import Counter


def main(path: str) -> None:
    text = open(path, encoding="utf-8", errors="replace").read()
    lost = [int(x) for x in re.findall(r"RTP packets lost: (\d+)", text)]
    res = re.findall(r"Resizing render image to (\d+x\d+)", text)
    dec = re.findall(r"Using hardware decoder: (\w+)", text)
    print(f"file                     : {path}")
    print(f"lines                    : {text.count(chr(10))}")
    print(f"WFB session received     : {text.count('SESSION')}")
    print(f"RTP stream detected (SDP): {text.count('Generated SDP')}")
    print(f"decoder started          : {text.count('Starting decoder')}")
    print(f"render resolution        : {sorted(set(res)) or '-'}")
    print(f"hardware decoder         : {sorted(set(dec)) or '-'}")
    print(f"software decoding forced : {'Software decoding is forced' in text}")
    print(f"key read error           : {text.count('Unable to read rx secret key')}")
    print(f"decrypt errors           : {text.count('Unable to decrypt')}")
    print(f"RTP loss events          : {len(lost)}")
    print(f"RTP packets lost (sum)   : {sum(lost)}")
    print(f"RTP max consecutive loss : {max(lost) if lost else 0}")
    print(f"'Could not find ref'     : {text.count('Could not find ref')}")
    print(f"'PPS id out of range'    : {text.count('PPS id out of range')}")
    nalu = Counter(re.findall(r"undecodable NALU: (\d+)", text))
    if nalu:
        print(f"skipped NAL unit types   : {dict(nalu)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
