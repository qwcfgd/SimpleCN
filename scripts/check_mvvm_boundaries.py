"""Fail on source dependencies that let UI or hardware cross MVVM boundaries."""
from pathlib import Path
import re
import sys

project = Path(__file__).resolve().parents[1]
root = project / "src"
widget_headers = {
    "QWidget", "QDialog", "QFileDialog", "QMainWindow", "QMessageBox",
    "QApplication", "QComboBox", "QLineEdit", "QPushButton", "QCheckBox",
    "QLabel", "QSpinBox", "QTableView", "QTreeView", "QStyledItemDelegate",
    "QItemDelegate", "QAbstractItemDelegate", "QCompleter", "QStyleOptionViewItem",
}
widget_names = {name.lower() for name in widget_headers}


def depends_on(include, path, directories):
    # Recognize both include-root paths and relative paths, such as ../views/X.h.
    candidates = [path.parent / include, root / include, project / include,
                  project / "resource" / include]
    return any(candidate.resolve().is_relative_to(directory.resolve())
               for candidate in candidates for directory in directories)


errors = []
folders = {name: root / name for name in
           ("domain", "model", "protocol", "infrastructure", "viewmodels", "views")}
folders.update({f"resource/{name}": project / "resource" / name for name in
                ("communication", "driverCan", "driverLin", "global")})
for folder, directory in folders.items():
    lower_layer = folder in ("domain", "model", "protocol", "infrastructure") or folder.startswith("resource/")
    for path in directory.rglob("*"):
        if path.suffix not in (".h", ".cpp"):
            continue
        for line, text in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if folder == "views" and re.search(r'\b(?:diag::Codec|diag::Database|SignalCodec|DatabaseImporter|SettingsStore)::|\bapplyCddCommunication\s*\(', text):
                errors.append(f"{path.relative_to(project)}:{line}: view must delegate parsing/encoding/persistence to a ViewModel")
            match = re.match(r'\s*#include\s+[<"]([^>"]+)', text)
            if not match:
                continue
            include = match[1].replace("\\", "/")
            reason = None
            if lower_layer and depends_on(include, path, [root / "views", root / "viewmodels"]):
                reason = "lower layers must not depend on presentation"
            if folder in ("views", "viewmodels") and (
                    depends_on(include, path, [project / "resource" / "driverCan", project / "resource" / "driverLin"])
                    or Path(include).name in {"ChannelWorker.h", "SoftwareChannel.h", "HardwareBackend.h", "PCANBasic.h", "PLinApi.h"}):
                reason = "presentation must not access SDK/worker implementation"
            if folder == "views" and depends_on(include, path, [root / "infrastructure"]):
                reason = "views must route infrastructure work through a ViewModel"
            if (folder == "viewmodels" or lower_layer) and (
                    include.startswith("QtWidgets/") or include == "QtWidgets"
                    or Path(include).stem.lower() in widget_names):
                reason = "non-View layers must not depend on widgets or editor delegates"
            if reason:
                errors.append(f"{path.relative_to(project)}:{line}: {reason}: {include}")
if errors:
    print("\n".join(errors))
    sys.exit(1)
print("MVVM boundaries passed (src layers and resource communication/drivers/global).")
