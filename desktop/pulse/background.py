"""Current-user Windows login startup; no administrator privileges required."""
import subprocess
import sys
from pathlib import Path

RUN_KEY = r'Software\Microsoft\Windows\CurrentVersion\Run'
VALUE_NAME = 'EPaperPulse'


def startup_command(executable=None, script=None):
    executable = executable or sys.executable
    if script is None and not getattr(sys, 'frozen', False) and executable == sys.executable:
        script = str(Path(__file__).resolve().parents[1] / 'main.py')
    if script:
        executable = executable.replace('python.exe', 'pythonw.exe')
    return subprocess.list2cmdline([executable, *([script] if script else []), '--start-hidden'])


def startup_enabled():
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            # An entry for an older location must remain possible to disable.
            return bool(winreg.QueryValueEx(key, VALUE_NAME)[0])
    except FileNotFoundError:
        return False


def set_startup(enabled):
    import winreg
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
        if enabled:
            winreg.SetValueEx(key, VALUE_NAME, 0, winreg.REG_SZ, startup_command())
        else:
            try:
                winreg.DeleteValue(key, VALUE_NAME)
            except FileNotFoundError:
                pass
