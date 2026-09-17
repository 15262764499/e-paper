"""Copy dependency license files next to the portable application's binaries."""
from importlib import metadata
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parent
target = root/'dist'/'Pulse'/'licenses'
target.mkdir(parents=True, exist_ok=True)
if (root/'assets'/'licenses').exists():
    shutil.copytree(root/'assets'/'licenses', target, dirs_exist_ok=True)
rows = []
python_license = Path(sys.base_prefix)/'LICENSE.txt'
if python_license.exists():
    shutil.copy2(python_license, target/'Python-LICENSE.txt')
for package in ('PySide6', 'PySide6_Essentials', 'PySide6_Addons', 'shiboken6',
                'psutil', 'pyserial', 'nvidia-ml-py', 'pyinstaller'):
    try:
        dist = metadata.distribution(package)
    except metadata.PackageNotFoundError:
        continue
    rows.append(f'{package} {dist.version}')
    for item in dist.files or []:
        name = str(item).lower()
        if any(term in name for term in ('license', 'copying')) and item.suffix.lower() in ('', '.txt', '.md', '.rst'):
            source = Path(dist.locate_file(item))
            if source.is_file():
                folder = target/package
                folder.mkdir(exist_ok=True)
                shutil.copy2(source, folder/source.name)
(target/'VERSIONS.txt').write_text('\n'.join(rows), encoding='utf-8')
for name in ('README.md', 'PROTOCOL.md'):
    shutil.copy2(root/name, target.parent/name)
print('Licenses and documentation:', target.parent)
