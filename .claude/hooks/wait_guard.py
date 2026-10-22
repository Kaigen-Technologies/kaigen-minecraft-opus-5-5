#!/usr/bin/env python3
# hz-hooks: PreToolUse:Edit|Write|MultiEdit=edit PreToolUse:Bash=run PreToolUse:PowerShell=run
"""No waiting through files.

bash_guard.py refuses `sleep` in a command, but reads only the command TEXT. A
run moved the sleep into a script written with the Write tool and ran
`bash m_perf.sh`: a "wait for the fast machine" loop whose parse broke, so it
slept 240 s per round and burned 33 minutes while the agent sat idle.

So the wait is refused at both doors: when a script file is WRITTEN with a wait
in it, and when a command RUNS a script file that contains one (however that
file got there). Owner-approved self-watchdogging tools go in guard_allow.txt as
`<path-substring>:script-wait`.
"""
import json
import os
import re
import shlex
import sys

SCRIPT_EXT = (".sh", ".bash", ".zsh", ".py", ".ps1", ".psm1", ".js", ".mjs", ".cjs", ".ts",
              ".bat", ".cmd")
WAIT = re.compile(
    r"(?:^|[;&|(\s{])(?:sleep\s+[\d.$]|Start-Sleep\b|time\.sleep\s*\(|asyncio\.sleep\s*\("
    r"|timeout(?:\.exe)?\s+/t\b|setTimeout\s*\(|setInterval\s*\(|tail\s+(?:-\S*[fF]\b|--follow)"
    r"|watch\s|Wait-Process\b|Wait-Job\b|Wait-Event\b)",
    re.M | re.I)
INTERPRETERS = {"bash", "sh", "zsh", "dash", "source", ".", "python", "python3", "py", "node",
                "tsx", "ts-node", "deno", "bun", "powershell", "powershell.exe", "pwsh", "pwsh.exe",
                "cmd", "cmd.exe", "call", "&"}
# interpreter flags that take a value (the value is not the script)
VALUE_FLAGS = {"-W", "-X", "-Q", "-O", "-ExecutionPolicy", "-Command", "-EncodedCommand",
               "-WorkingDirectory", "-OutputFormat", "-InputFormat", "-WindowStyle", "-ConfigurationName",
               "--import", "--require", "-r", "--loader"}
SEGMENT = re.compile(r"\|\||&&|;|\||\n")


def allowlisted(path):
    here = os.path.dirname(os.path.abspath(__file__))
    try:
        with open(os.path.join(here, "guard_allow.txt"), errors="replace") as f:
            lines = [l.strip() for l in f if l.strip() and not l.lstrip().startswith("#")]
    except OSError:
        return False
    p = path.replace("\\", "/")
    for line in lines:
        sub, _, rule = line.rpartition(":")
        if rule == "script-wait" and sub and sub in p:
            return True
    return False


def refuse(path, text, m):
    start = text.rfind("\n", 0, m.start()) + 1
    end = text.find("\n", m.end())
    line = text[start:end if end >= 0 else len(text)].strip()
    sys.stderr.write(
        "REFUSED: %s waits: `%s`\n\n"
        "A script that sleeps, polls or follows is the same wait bash_guard refuses in a\n"
        "command, moved into a file. Never wait: run the command once and read its result;\n"
        "a background job notifies when it is done. A self-watchdogging tool is exempted only\n"
        "by the owner, in guard_allow.txt as `<path-substring>:script-wait`.\n"
        % (path, line[:160]))
    return 2


def scripts_run(cmd, cwd):
    """Files a shell command executes as scripts, resolved against cwd and any `cd` before them."""
    out = []
    here = cwd
    for seg in SEGMENT.split(cmd):
        try:
            toks = shlex.split(seg, posix=True)
        except ValueError:
            toks = seg.split()
        # env assignments and wrappers in front of the program
        while toks and (re.match(r"^\w+=", toks[0]) or toks[0] in ("env", "time", "nohup", "exec", "command")):
            toks = toks[1:]
        if not toks:
            continue
        head = toks[0]
        if head in ("cd", "Set-Location", "pushd") and len(toks) > 1:
            d = toks[1]
            here = d if os.path.isabs(d) or re.match(r"^[A-Za-z]:[\\/]|^/", d) else os.path.join(here, d)
            continue
        target = None
        base = os.path.basename(head).lower()
        if base in INTERPRETERS:
            i = 1
            while i < len(toks):
                t = toks[i]
                if t.lower() in ("-file", "-f") and base.startswith(("powershell", "pwsh")):
                    target = toks[i + 1] if i + 1 < len(toks) else None
                    break
                if t in ("-c", "-m", "-e", "--eval", "-Command", "/c", "/C") and not base.startswith(("powershell", "pwsh")):
                    break
                if t in VALUE_FLAGS:
                    i += 2
                    continue
                if t.startswith("-") or t.startswith("/"):
                    if t.startswith("/") and os.path.splitext(t)[1].lower() in SCRIPT_EXT:
                        target = t
                        break
                    i += 1
                    continue
                target = t
                break
        elif head.lower().endswith(SCRIPT_EXT) or head.startswith("./"):
            target = head
        if target:
            out.append(target if os.path.isabs(target) or re.match(r"^[A-Za-z]:[\\/]", target)
                       else os.path.join(here, target))
    return out


def native(path):
    """/d/x -> D:/x on Windows so Git Bash paths open from Python."""
    m = re.match(r"^/([A-Za-z])/(.*)$", path)
    if m and os.name == "nt":
        return "%s:/%s" % (m.group(1).upper(), m.group(2))
    return path


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    try:
        data = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0
    if mode == "edit":
        for e in data.get("hz_edits") or []:
            path, text = e.get("path") or "", e.get("added") or ""
            if path.lower().endswith(SCRIPT_EXT) and not allowlisted(path):
                m = WAIT.search(text)
                if m:
                    return refuse(path, text, m)
        return 0
    if mode == "run":
        cmd = (data.get("tool_input") or {}).get("command") or ""
        cwd = data.get("cwd") or os.getcwd()
        for target in scripts_run(cmd, cwd):
            path = native(target)
            if not os.path.isfile(path) or allowlisted(path):
                continue
            try:
                with open(path, errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            m = WAIT.search(text)
            if m:
                return refuse(target, text, m)
    return 0


if __name__ == "__main__":
    sys.exit(main())
