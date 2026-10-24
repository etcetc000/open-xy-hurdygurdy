"""Regenerate the bow curve in tables.h from the documented friction model."""
from pathlib import Path
import re

values = [round(min(0.98, max(0.01, (i / 256 + 0.75) ** -4)) * 2**31) // 65536
          for i in range(1025)]
table = "const opxy::u16 hg_bow15[1025] = {\n" + "".join(
    "    " + ", ".join(map(str, values[i:i + 8])) + ",\n"
    for i in range(0, len(values), 8)) + "};"
path = Path(__file__).resolve().parents[1] / "tables.h"
source, count = re.subn(r"const opxy::u16 hg_bow15\[\d+\] = \{.*?\n\};",
                        lambda _: table, path.read_text(), flags=re.S)
if count != 1:
    raise SystemExit("Expected exactly one bow curve table")
path.write_text(source)
