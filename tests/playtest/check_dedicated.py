#!/usr/bin/env python3
"""Check the logs of the headless dedicated-server scenarios.

Usage:
    check_dedicated.py clean    <log> [<log> ...]   exit 0, clean shutdown, no crash markers
    check_dedicated.py wepfire  <log>               wepfireall sweep (scenarios/wepfire_late.cfg)
    check_dedicated.py grenades <log>               grenadethrowall (scenarios/grenadeall.cfg)
    check_dedicated.py botkill  <log>               botkill (scenarios/botkill.cfg)

Each check prints one PASS/FAIL line per rule and exits 1 when any rule fails. Colour codes in
the engine log are stripped first.
"""
import json
import re
import sys

CRASH = re.compile(r"Crash|signal [0-9]|Sys_Error|Host_Error|Segmentation|Assertion")


def read(path):
    return re.sub(r"\x1b\[[0-9;]*m", "", open(path, errors="replace").read())


def events(text, tag="SGTCEVENT"):
    for m in re.finditer(tag + r" (\{.*\})", text):
        try:
            yield json.loads(m.group(1))
        except ValueError:
            pass


class Report:
    def __init__(self):
        self.failed = 0

    def rule(self, ok, text):
        print(("PASS " if ok else "FAIL ") + text)
        if not ok:
            self.failed += 1


def check_clean(rep, path, text):
    exits = re.findall(r"EXIT=(\d+)", text)
    rep.rule(bool(exits) and exits[-1] == "0", "%s: exit code %s" % (path, exits[-1] if exits else "missing"))
    rep.rule("Server shutdown" in text, "%s: clean shutdown" % path)
    rep.rule(not CRASH.search(text), "%s: no crash markers" % path)


def check_wepfire(rep, path, text):
    rows = [e for e in events(text) if e.get("event") == "wepfire_result"]
    given = sum(1 for r in rows if r.get("given"))
    primary = sum(1 for r in rows if r.get("primary"))
    secondary = sum(1 for r in rows if r.get("secondary"))
    rep.rule(len(rows) == 38, "wepfire: 38 weapon classes tried (%d)" % len(rows))
    rep.rule(given >= 31, "wepfire: at least 31 classes given (%d)" % given)
    rep.rule(primary >= 31, "wepfire: at least 31 primary attacks fired (%d)" % primary)
    rep.rule(secondary >= 11, "wepfire: at least 11 secondary attacks fired (%d)" % secondary)
    summary = [e for e in events(text, "SGTCTEST") if e.get("cmd") == "wepfireall" and "self_killed" in e]
    rep.rule(bool(summary) and summary[-1]["self_killed"] == 0, "wepfire: no self-kills")


def check_grenades(rep, path, text):
    thrown = {e["class"] for e in events(text) if e.get("event") == "grenade_result" and e.get("thrown") == 1}
    want = {"weapon_smokegrenade", "weapon_flashbang", "weapon_mustardgrenade",
            "weapon_narcogrenade", "weapon_nervegrenade", "weapon_gacgoa"}
    rep.rule(want <= thrown, "grenades: all six classes thrown (missing: %s)" % sorted(want - thrown))


def check_botkill(rep, path, text):
    kills = [e for e in events(text) if e.get("event") == "bot_kill"]
    rep.rule(len(kills) >= 2, "botkill: at least two kills (%d)" % len(kills))
    rep.rule(all(k.get("alive_after") == 0 for k in kills), "botkill: every victim died")


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    mode, paths = sys.argv[1], sys.argv[2:]
    rep = Report()
    for path in paths:
        text = read(path)
        if mode == "clean":
            check_clean(rep, path, text)
            continue
        check_clean(rep, path, text)
        {"wepfire": check_wepfire, "grenades": check_grenades, "botkill": check_botkill}[mode](rep, path, text)
    return 1 if rep.failed else 0


if __name__ == "__main__":
    sys.exit(main())
