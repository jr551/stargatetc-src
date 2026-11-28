#!/usr/bin/env python3
"""Write a userconfig.cfg scenario for the GUI client (see run_gui.sh).

Usage:
    gui_scenarios.py map     <out> <map>               join, take a briefing and an in-game screenshot
    gui_scenarios.py menus   <out>                     one screenshot each of briefing, team, MOTD, help, loadout, pack
    gui_scenarios.py chain   <out>                     chain 5 -> 11 -> 10, then autopick (menu chain hang regression)
    gui_scenarios.py weapons <out> <weapon list file>  give, use and fire every listed weapon (client effects)
    gui_scenarios.py stamina <out>                     set the player's stamina to 20 and sample it for 25 s of game time
    gui_scenarios.py flash   <out>                     lift the player into open air and spawn a flash at the eyes

A GUI run executes the file one command per frame: `wait` is one frame, so the scripts build
longer delays from nested aliases (a long run of plain `wait` lines overflows the command buffer).
Screenshots land in <install>/stargatetc/scrshots. `vguimenuyoyo <id>` opens a menu from the
server side, `hidevgui` clears the menu chain, `echo STEP_<name>` marks the log.
"""
import sys

PRELUDE = [
    'maxplayers 8',
    'sgtc_bot_quota 2',
    'alias w10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"',
    'alias w100 "w10;w10;w10;w10;w10;w10;w10;w10;w10;w10"',
]


def scenario_map(mapname):
    return PRELUDE + ['mp_wepselmode 0', 'map ' + mapname, 'w100;w100;w100;w100;w100',
                      'echo STEP_briefing', 'screenshot', 'w10', 'hidevgui', 'w10', 'autopick',
                      'w100;w100;w100;w100', 'hidevgui', 'w100', 'echo STEP_ingame', 'screenshot',
                      'w100', 'quit']


def scenario_menus():
    lines = PRELUDE + ['mp_wepselmode 2', 'map sg_nox', 'w100;w100;w100;w100;w100']
    for name, menu in (('briefing', None), ('team', 11), ('motd', 5), ('help', 4),
                       ('weapons', 12), ('packs', 60)):
        if menu is not None:
            lines += ['hidevgui', 'w10', 'vguimenuyoyo %d' % menu]
        lines += ['w10;w10;w10;w10;w10;w10', 'echo SHOT_' + name, 'screenshot', 'w10;w10']
    return lines + ['quit']


def scenario_chain():
    return PRELUDE + ['mp_wepselmode 2', 'map sg_nox', 'w100;w100;w100;w100;w100', 'hidevgui', 'w10',
                      'vguimenuyoyo 5', 'w10', 'vguimenuyoyo 11', 'w10', 'vguimenuyoyo 10', 'w10',
                      'echo STEP_autopick', 'autopick', 'w100;w100;w100', 'echo STEP_after',
                      'screenshot', 'w100', 'quit']


def scenario_stamina():
    # sgtc_test_enable lets `sgtc_test "stamina_20"` set the host's stamina; `players` prints it.
    lines = ['maxplayers 8', 'sgtc_bot_quota 0', 'mp_wepselmode 0', 'sv_cheats 1', 'sgtc_test_enable 1',
             PRELUDE[2], PRELUDE[3], 'alias w1000 "w100;w100;w100;w100;w100;w100;w100;w100;w100;w100"',
             'map sg_nox', 'w100;w100;w100;w100;w100', 'hidevgui', 'w10', 'autopick', 'w100;w100',
             'hidevgui', 'w10', 'god', 'echo STEP_set', 'sgtc_test "stamina_20"', 'w10',
             'sgtc_test "players"', 'w1000', 'echo STEP_t1', 'sgtc_test "players"', 'w1000',
             'echo STEP_t2', 'sgtc_test "players"', 'w100', 'quit']
    return lines


def scenario_flash():
    # sgtc_test "flashair" lifts the host into open air and spawns an info_flash at the eyes. Two
    # screenshots: before, and about four seconds after, when a surviving flash has faded the screen
    # to white (average luma above 200 against about 90 normally; the old code gave 76).
    return ['maxplayers 8', 'sgtc_bot_quota 0', 'mp_wepselmode 0', 'sv_cheats 1', 'sgtc_test_enable 1',
            PRELUDE[2], PRELUDE[3], 'map sg_nox', 'w100;w100;w100;w100;w100', 'hidevgui', 'w10', 'autopick',
            'w100;w100', 'hidevgui', 'w10', 'god', 'w100', 'echo STEP_before', 'screenshot', 'w10',
            'sgtc_test "flashair"', 'w100;w100;w100', 'echo STEP_after', 'screenshot', 'w100', 'quit']


def scenario_weapons(path):
    weapons = [w.strip() for w in open(path) if w.strip()]
    lines = PRELUDE + ['mp_wepselmode 0', 'sv_cheats 1', 'sgtc_fxlog 1',
                       'alias fire1 "+attack;w10;-attack;w10"', 'alias fire2 "+attack2;w10;-attack2;w10"',
                       'map sg_nox', 'w100;w100;w100;w100;w100', 'hidevgui', 'w10', 'classe 2', 'w100',
                       'god', 'notarget', 'w100']
    for w in weapons:
        lines += ['echo WEP_START_' + w, 'give ' + w, 'w10', 'use ' + w, 'w100', 'fire1', 'fire1',
                  'fire2', 'w10', 'echo WEP_END_' + w]
    return lines + ['echo SWEEP_DONE', 'w100', 'screenshot', 'w10', 'quit']


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    kind, out = sys.argv[1], sys.argv[2]
    if kind == 'map' and len(sys.argv) == 4:
        lines = scenario_map(sys.argv[3])
    elif kind == 'menus':
        lines = scenario_menus()
    elif kind == 'chain':
        lines = scenario_chain()
    elif kind == 'flash':
        lines = scenario_flash()
    elif kind == 'stamina':
        lines = scenario_stamina()
    elif kind == 'weapons' and len(sys.argv) == 4:
        lines = scenario_weapons(sys.argv[3])
    else:
        raise SystemExit(__doc__)
    open(out, 'w').write('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
