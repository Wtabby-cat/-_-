"""Ensure the shipping Keil target builds every IH application module.

Host tests link sources explicitly and cannot detect omissions in .uvprojx.
"""
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parent.parent
project = root / "MDK-ARM" / "IH_incomp.uvprojx"
targets = ET.parse(project).getroot().findall("./Targets/Target")
target = next(t for t in targets if t.findtext("TargetName") == "IH_incomp")
included = {}
for entry in target.findall("./Groups/Group/Files/File"):
    name = entry.findtext("FilePath", "").replace("\\", "/")
    path = (project.parent / name).resolve()
    enabled = entry.findtext("./FileOption/CommonProperty/IncludeInBuild", "2")
    if enabled != "0":
        included[str(path).casefold()] = path
missing = [p.name for p in sorted((root / "Core" / "Src").glob("ih_*.c"))
           if str(p.resolve()).casefold() not in included]
if missing:
    raise SystemExit("FAIL: IH_incomp Keil target omits: " + ", ".join(missing))
print("PASS: Keil target includes every IH application module, including search/PLL")
