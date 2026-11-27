#!/usr/bin/env python3
"""Regression checks for the shipped Xash command-menu data and label buffers.

Usage:
    python3 tests/commandmenu_regression.py /path/to/stargatetc

The command-menu root is implicit in TeamFortressViewport::CreateCommandMenu,
so a structural depth of one is used here. The final root closing brace is
required; a second one is an error.
"""

from dataclasses import dataclass, field
from pathlib import Path
import re
import sys


@dataclass
class Token:
    value: str
    line: int
    column: int


@dataclass
class Button:
    label: str
    command: str
    modifier: str = ""
    map_name: str = ""
    submenu: object = None


@dataclass
class Menu:
    name: str
    parent: object = None
    buttons: list = field(default_factory=list)


def tokenize(text):
    tokens = []
    anomalies = []
    pos = 0
    line = 1
    column = 1
    length = len(text)

    def advance(value):
        nonlocal pos, line, column
        pos += len(value)
        pieces = value.split("\n")
        if len(pieces) > 1:
            line += len(pieces) - 1
            column = len(pieces[-1]) + 1
        else:
            column += len(value)

    while pos < length:
        char = text[pos]
        if char <= " ":
            advance(char)
            continue
        if text.startswith("//", pos):
            end = text.find("\n", pos)
            advance(text[pos:] if end < 0 else text[pos:end])
            continue

        start_line, start_column = line, column
        if char in "{}":
            tokens.append(Token(char, start_line, start_column))
            advance(char)
            continue

        if char == '"':
            advance(char)
            value = []
            closed = False
            while pos < length:
                char = text[pos]
                if char == "\\" and pos + 1 < length and text[pos + 1] == '"':
                    value.append('"')
                    advance('\\"')
                elif char == '"':
                    advance(char)
                    closed = True
                    break
                else:
                    value.append(char)
                    advance(char)
            if not closed:
                anomalies.append(f"unterminated quote at {start_line}:{start_column}")
            value = "".join(value)
            if "\n" in value or "\r" in value:
                anomalies.append(
                    f"multiline quoted token at {start_line}:{start_column}: {value!r}"
                )
            tokens.append(Token(value, start_line, start_column))
            continue

        start = pos
        while pos < length and text[pos] > " " and text[pos] not in "{}":
            if text.startswith("//", pos):
                break
            advance(text[pos])
        tokens.append(Token(text[start:pos], start_line, start_column))

    return tokens, anomalies


def parse_menu(text, require_root_close=False):
    tokens, anomalies = tokenize(text)
    root = Menu("root")
    current = root
    root_closed = False
    index = 0

    while index < len(tokens):
        token = tokens[index]
        index += 1
        if token.value == "}":
            if current.parent is None:
                if root_closed:
                    anomalies.append(f"extra root closing brace at {token.line}:{token.column}")
                else:
                    root_closed = True
            else:
                current = current.parent
            continue

        modifier = ""
        map_name = ""
        if token.value in ("CUSTOM", "TOGGLE") or token.value.startswith("TEAM"):
            modifier = token.value
            if index >= len(tokens):
                anomalies.append(f"missing bound key after {modifier} at {token.line}:{token.column}")
                break
            token = tokens[index]
            index += 1
        if token.value == "MAP":
            modifier = "MAP"
            if index + 1 >= len(tokens):
                anomalies.append(f"truncated MAP entry at {token.line}:{token.column}")
                break
            map_name = tokens[index].value
            token = tokens[index + 1]
            index += 2

        if index + 1 >= len(tokens):
            anomalies.append(f"truncated button entry at {token.line}:{token.column}")
            break
        label_token, command_token = tokens[index], tokens[index + 1]
        index += 2
        button = Button(label_token.value, command_token.value, modifier, map_name)
        current.buttons.append(button)
        if command_token.value == "{":
            button.submenu = Menu(label_token.value, current)
            current = button.submenu

    if current is not root:
        anomalies.append(f"unclosed submenu {current.name!r}")
    if require_root_close and not root_closed:
        anomalies.append("missing root closing brace")
    return root, anomalies


def read_titles(path):
    entries = {}
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    index = 0
    while index < len(lines):
        name = lines[index].strip()
        index += 1
        if not name.startswith("CM_"):
            continue
        while index < len(lines) and not lines[index].strip():
            index += 1
        if index >= len(lines) or lines[index].strip() != "{":
            continue
        index += 1
        value = []
        while index < len(lines) and lines[index].strip() != "}":
            value.append(lines[index].strip())
            index += 1
        if index < len(lines):
            index += 1
        entries[f"#{name}"] = " ".join(value).strip()
    return entries


def all_buttons(menu):
    for button in menu.buttons:
        yield button
        if button.submenu is not None:
            yield from all_buttons(button.submenu)


def self_test():
    valid = '''
" " "#CM_HELP"
{
  " " "#CM_TIME_LEFT" "timeleft"
}
" " "#CM_EXIT_GAME" "exit"
}
'''
    root, anomalies = parse_menu(valid)
    assert not anomalies, anomalies
    assert [button.label for button in root.buttons] == ["#CM_HELP", "#CM_EXIT_GAME"]

    _, anomalies = parse_menu(valid + "}\n")
    assert any("extra root closing brace" in item for item in anomalies), anomalies

    _, anomalies = parse_menu(valid.rsplit("}", 1)[0], require_root_close=True)
    assert any("missing root closing brace" in item for item in anomalies), anomalies

    _, anomalies = parse_menu('" " "#CM_HELP" "unterminated\n')
    assert any("unterminated quote" in item for item in anomalies), anomalies

    malformed_quote = '''
" " "#CM_TEAM_MESSAGE"
{
  " " "#CM_ENEMY_SPOTTED" "say_team Enemy Spotted!
  " " "#CM_MEDIC" "say_team I Need A Medic!"
}
}
'''
    _, anomalies = parse_menu(malformed_quote)
    assert any("multiline quoted token" in item for item in anomalies), anomalies


def main(argv):
    if len(argv) != 2:
        print(f"usage: {Path(argv[0]).name} /path/to/stargatetc", file=sys.stderr)
        return 2

    self_test()
    game_dir = Path(argv[1])
    source_dir = Path(__file__).resolve().parents[1]
    header = (source_dir / "cl_dll/vgui_TeamFortressViewport.h").read_text(encoding="utf-8")
    parser = (source_dir / "cl_dll/vgui_TeamFortressViewport.cpp").read_text(encoding="utf-8")
    match = re.search(r"#define\s+MAX_BUTTON_SIZE\s+(\d+)", header)
    if not match:
        raise AssertionError("MAX_BUTTON_SIZE is not defined")
    button_size = int(match.group(1))
    # RecalculateText reserves five prefix characters and one byte for NUL.
    visible_capacity = button_size - 6
    assert "char cText[MAX_BUTTON_SIZE]" in parser
    assert "char szLastButtonText[MAX_BUTTON_SIZE]" in parser
    assert "sizeof( cText )" in parser

    titles = read_titles(game_dir / "titles.txt")
    expected_root_order = [
        "#CM_HELP",
        "#CM_CHANGE_TEAM",
        "#CM_CHANGE_WEAPONS",
        "#CM_TEAM_MESSAGE",
        "#CM_SERVER_ADMIN",
        "#CM_RCON",
        "#CM_PERSONAL_SETTINGS",
        "#CM_EXIT_GAME",
    ]
    failures = []
    total_buttons = 0
    command_root = None

    for filename in ("commandmenu.txt", "spectatormenu.txt", "spectcammenu.txt"):
        root, anomalies = parse_menu(
            (game_dir / filename).read_text(encoding="utf-8", errors="replace"),
            require_root_close=(filename == "commandmenu.txt"),
        )
        if anomalies:
            failures.extend(f"{filename}: {item}" for item in anomalies)
        buttons = list(all_buttons(root))
        total_buttons += len(buttons)
        if filename == "commandmenu.txt":
            command_root = root
            actual_order = [button.label for button in root.buttons]
            if actual_order != expected_root_order:
                failures.append(f"commandmenu.txt: root order changed: {actual_order!r}")
        for button in buttons:
            if button.label.startswith("#CM_") and button.label not in titles:
                failures.append(f"{filename}: missing title {button.label}")
                continue
            label = titles.get(button.label, button.label)
            if len(label) > visible_capacity:
                failures.append(
                    f"{filename}: {button.label} renders {len(label)} characters; "
                    f"capacity is {visible_capacity}"
                )

    assert command_root is not None
    commands = {button.label: button.command for button in all_buttons(command_root)}
    expected_commands = {
        "#CM_ENEMY_SPOTTED": "say_team Enemy Spotted!",
        "#CM_MEDIC": "say_team I Need A Medic!",
    }
    for label, command in expected_commands.items():
        if commands.get(label) != command:
            failures.append(f"commandmenu.txt: {label} command is {commands.get(label)!r}")

    expected_full_labels = {
        "#CM_ENEMY_IS_HOLDING_THE_STARGATE": "Enemy Is Holding The Stargate",
        "#CM_MAP_SPECIFIC_SG_MIDDLEAGE": "Map Specific (sg_middleage)",
        "#CM_MAP_SPECIFIC_SG_NEWSTRIKE": "Map Specific (sg_newstrike)",
        "#CM_ENEMY_HAVE_SECURED_THE_GATE": "Enemy Have Secured The Gate",
    }
    for label, value in expected_full_labels.items():
        if titles.get(label) != value:
            failures.append(f"titles.txt: {label} is {titles.get(label)!r}")

    if failures:
        print("FAIL")
        for failure in failures:
            print(f"  {failure}")
        return 1

    print(
        f"PASS: 3 menus, {total_buttons} buttons, "
        f"label capacity {visible_capacity}, root order and commands unchanged"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
