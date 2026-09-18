"""Fail on source dependencies that let UI or hardware cross MVVM boundaries."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1] / "src"
errors = []
for folder in ("domain", "model", "protocol", "infrastructure", "viewmodels", "views"):
    for path in (root / folder).rglob("*"):
        if path.suffix not in (".h", ".cpp"):
            continue
        for line, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if folder == "views" and re.search(r'\b(?:diag::Codec|diag::Database|SignalCodec|DatabaseImporter|SettingsStore)::|\bapplyCddCommunication\s*\(', text):
                errors.append(f"{path.relative_to(root)}:{line}: view must delegate parsing/encoding/persistence to a ViewModel")
            match = re.match(r'\s*#include\s+[<"]([^>"]+)', text)
            if not match:
                continue
            include = match[1]
            reason = None
            if folder in ("domain", "model", "protocol", "infrastructure") and include.startswith(("views/", "viewmodels/")):
                reason = "lower layers must not depend on presentation"
            if folder in ("views", "viewmodels") and ("driverCan/" in include or "driverLin/" in include or "ChannelWorker.h" in include):
                reason = "presentation must not access SDK/worker implementation"
            if folder == "views" and include.startswith("infrastructure/"):
                reason = "views must route infrastructure work through a ViewModel"
            if folder == "viewmodels" and include in {"QWidget", "QDialog", "QFileDialog", "QMainWindow", "QMessageBox"}:
                reason = "ViewModels must not create or own widgets"
            if reason:
                errors.append(f"{path.relative_to(root)}:{line}: {reason}: {include}")
if errors:
    print("\n".join(errors))
    sys.exit(1)
print("MVVM boundaries passed (domain/model/protocol/infrastructure/viewmodels/views).")
