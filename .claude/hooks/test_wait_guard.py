#!/usr/bin/env python3
"""Regression test for wait_guard.py, run through _dispatch.py exactly as the harness calls it.

The first BLOCK cases are the bypass that cost 33 minutes: a sleep moved into a script
written with the Write tool, then run with `bash`. A hook that crashes counts as a failure.
The wait words are assembled at runtime so this file does not trip the guard it tests.

usage: python3 .claude/hooks/test_wait_guard.py
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DISPATCH = os.path.join(HERE, "_dispatch.py")
SL = "sl" + "eep"
SS = "Start-" + "Sleep"
TS = "time." + SL


def dispatch(payload):
    r = subprocess.run([sys.executable, DISPATCH, "PreToolUse"], input=json.dumps(payload),
                       capture_output=True, text=True, timeout=60)
    return r.returncode, r.stderr


def write(path, content):
    return {"tool_name": "Write", "tool_input": {"file_path": path, "content": content}, "cwd": HERE}


def edit(path, new):
    return {"tool_name": "Edit", "tool_input": {"file_path": path, "old_string": "x", "new_string": new},
            "cwd": HERE}


def bash(cmd, cwd):
    return {"tool_name": "Bash", "tool_input": {"command": cmd}, "cwd": cwd}


def ps(cmd, cwd):
    return {"tool_name": "PowerShell", "tool_input": {"command": cmd}, "cwd": cwd}


def main():
    d = tempfile.mkdtemp(prefix="wait_guard_")
    files = {
        "m_perf.sh": "#!/bin/bash\nfor i in $(seq 1 40); do\n  run_probe\n  %s 240\ndone\n" % SL,
        "poll.ps1": "while (-not (Test-Path x)) { %s -Seconds 5 }\n" % SS,
        "wait.py": "import time\nwhile True:\n    %s(30)\n" % TS,
        "clean.sh": "#!/bin/bash\necho build\n./hzbuild test\n",
        "clean.py": "print('ok')\n",
    }
    for name, text in files.items():
        with open(os.path.join(d, name), "w", newline="\n") as f:
            f.write(text)
    fwd = d.replace("\\", "/")
    gitbash = "/" + fwd[0].lower() + fwd[2:] if len(fwd) > 1 and fwd[1] == ":" else fwd

    cases = [
        # (should_block, label, payload)
        (True, "Write a .sh whose loop sleeps (the exact bypass)", write(fwd + "/new.sh", files["m_perf.sh"])),
        (True, "bash a script that sleeps, absolute git-bash path + redirect",
         bash('bash "%s/m_perf.sh" > /d/hz-share/m_perf.out 2>&1; cat /d/hz-share/m_perf.out' % gitbash, HERE)),
        (True, "bash a script that sleeps, Windows path", bash('bash "%s/m_perf.sh"' % fwd, HERE)),
        (True, "cd then run relative ./script", bash('cd "%s" && ./m_perf.sh' % fwd, HERE)),
        (True, "sh relative to cwd", bash("sh m_perf.sh", fwd)),
        (True, "source a sleeping script", bash("source %s/m_perf.sh" % fwd, HERE)),
        (True, "Write a .ps1 with " + SS, write(fwd + "/n.ps1", files["poll.ps1"])),
        (True, "Edit adds %s( to a .py" % TS, edit(fwd + "/clean.py", "    %s(5)\n" % TS)),
        (True, "python with flags runs a sleeping .py", bash("python -W ignore %s/wait.py" % fwd, HERE)),
        (True, "powershell -ExecutionPolicy Bypass -File a polling .ps1",
         bash('powershell -ExecutionPolicy Bypass -File "%s/poll.ps1"' % fwd, HERE)),
        (True, "PowerShell tool: call operator on a polling .ps1", ps('& "%s/poll.ps1"' % fwd, HERE)),
        (True, "Write a .js with setInterval", write(fwd + "/w.js", "setInterval(() => check(), 1000);\n")),
        (True, "Write a .sh that follows a log", write(fwd + "/t.sh", "tail -f out.log | grep DONE\n")),
        (False, "Write a markdown note mentioning a sleep", write(fwd + "/n.md", "we %s 240 here\n" % SL)),
        (False, "Write a clean script", write(fwd + "/ok.sh", files["clean.sh"])),
        (False, "bash a clean script", bash('bash "%s/clean.sh"' % fwd, HERE)),
        (False, "python -c inline", bash('python -c "print(1)"', HERE)),
        (False, "python runs a clean file", bash("python %s/clean.py" % fwd, HERE)),
        (False, "plain build command", bash("cd hz_tests && ../hzbuild wasm release run test", HERE)),
        (False, "C source with a sleep call is not a script", write(fwd + "/a.c", "os_%s(10);\n" % SL)),
    ]
    fails = 0
    for block, label, payload in cases:
        code, err = dispatch(payload)
        crashed = "Traceback" in err or "failed to run" in err
        blocked = code == 2 and "REFUSED" in err
        ok = (blocked == block) and not crashed
        fails += not ok
        print("%s  %-5s %s%s" % ("ok  " if ok else "FAIL", "BLOCK" if block else "allow", label,
                                 "" if ok else "\n      exit %d: %s" % (code, err.strip()[:300])))
    print("\n%d/%d cases correct" % (len(cases) - fails, len(cases)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
