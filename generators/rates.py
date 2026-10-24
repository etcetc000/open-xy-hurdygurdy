"""Retune original poles and envelope constants for the reduced-rate renderer."""
import json
import math
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
reference = json.loads((root / "generators/filter_reference.json").read_text())
factor = 4


def resonator(values):
    _, a1, a2 = values  # Stored coefficients are halved Q31 values.
    radius = math.sqrt(2 * a2 / 2**31)
    angle = math.acos((a1 / 2**31) / radius)
    radius = radius**factor
    return [round((1 - radius * radius) * 2**31 / 4),
            round(radius * math.cos(angle * factor) * 2**31),
            round(radius * radius * 2**31 / 2)]


body = []
for index in range(0, len(reference["body"]), 3):
    body.extend(resonator(reference["body"][index:index + 3]))
table = "const i32 hg_body[153] = {\n" + "".join(
    "    " + ", ".join(map(str, body[i:i + 9])) + ",\n"
    for i in range(0, len(body), 9)) + "};"
path = root / "tables.h"
source, count = re.subn(r"const i32 hg_body\[153\] = \{.*?\n\};",
                        lambda _: table, path.read_text(), flags=re.S)
if count != 1:
    raise SystemExit("Expected one soundbox table")
path.write_text(source)

constants = dict(zip(("rattle_b0", "rattle_a1", "rattle_a2"), resonator(reference["rattle"])))
constants.update({name: round((value / 2**31)**(64 if name == "click_decay" else factor) * 2**31)
                  for name, value in reference["decays"].items()})
path = root / "engine.cpp"
source = path.read_text()
for name, value in constants.items():
    source, count = re.subn(r"(const i32 " + name + r" = )-?\d+",
                            lambda match: match[1] + str(value), source)
    if count != 1:
        raise SystemExit("Expected one constant: " + name)
path.write_text(source)
