#!/usr/bin/env python3
"""compare_layouts.py BUILD_DIR OUTDIR: every save structure's layout as the port compiles it and as a 32-bit
(-m32) build of the same file does, compared field by field.

i386 puts 64-bit members on 4-byte boundaries and has 4-byte pointers, as the DS's compiler does, so a structure
that differs here is laid out differently from the DS's save (2026-11-05: all 40 match, and the 32-bit sizes
give PKHeX's Platinum block sizes and offsets). Needs a configured build dir (compile_commands.json, generated
headers), gdb, gcc-multilib, and SDL2's headers; types are read from the objects' DWARF by dump_layout.py.
Run it after changing a saved structure; a "DIFFERS" line means the port's save stops being the DS's.
"""
import json, os, re, shlex, subprocess, sys

S = os.path.dirname(os.path.abspath(__file__))
import shutil, glob
build, outdir = sys.argv[1], sys.argv[2]
root = os.path.dirname(os.path.abspath(build))
os.makedirs(outdir, exist_ok=True)
CHUNKS = {  # type: file
    'SystemData': 'src/system_data.c', 'PlayerSave': 'src/save_player.c', 'Party': 'src/party.c', 'Bag': 'src/bag.c',
    'VarsFlags': 'src/vars_flags.c', 'Poketch': 'src/poketch.c', 'FieldOverworldState': 'src/field_overworld_state.c',
    'Pokedex': 'src/pokedex.c', 'Daycare': 'src/daycare_save.c', 'PalPad': 'src/pal_pad.c',
    'MiscSaveBlock': 'src/savedata_misc.c', 'FieldOverworldSave': 'src/field_overworld_state.c',
    'Underground': 'src/underground.c', 'BattleRegulationData': 'src/battle_regulation.c', 'ImageClips': 'src/unk_020298BC.c',
    'Mailbox': 'src/mail.c', 'PoffinCase': 'src/poffin.c', 'RecordMixedRNG': 'src/record_mixed_rng.c',
    'JournalEntry': 'src/journal.c', 'TrainerCaseSaveData': 'src/trainer_case_save_data.c', 'GameRecords': 'src/game_records.c',
    'SealCase': 'src/ball_seal_info.c', 'ChatotCry': 'src/chatot_cry.c', 'BattleFrontierSave': 'src/battle_frontier_save.c',
    'SpecialEncounter': 'src/special_encounter.c', 'GlobalTrade': 'src/global_trade.c', 'TVBroadcast': 'src/unk_0202E2CC.c',
    'Rankings': 'src/rankings.c', 'WiFiList': 'src/wifi_list.c', 'WiFiHistory': 'src/wifi_history_save_data.c',
    'MysteryGift': 'src/mystery_gift.c', 'MigratedPokemon': 'src/pal_park_transfers.c',
    'LinkContestRecords': 'src/link_contest_records.c', 'UnlockedEasyChatWords': 'src/easy_chat_words.c',
    'UnkStruct_02030CEC': 'src/unk_02030CE8.c', 'WiFiPlazaQuestion': 'src/wifi_plaza_question_data.c',
    'PCBoxes': 'src/pc_boxes.c', 'HallOfFame': 'src/hall_of_fame_entries.c',
    'BattleHallWinRecords': 'src/battle_hall_win_records.c', 'BattleRecording': 'src/unk_0202F1D4.c',
}
# SDL's config header is arch-specific on Debian/Ubuntu; the x86_64 one does for type layouts
INC32 = os.path.join(outdir, 'inc32')
os.makedirs(os.path.join(INC32, 'SDL2'), exist_ok=True)
for cfg in glob.glob('/usr/include/*-linux-gnu/SDL2/_real_SDL_config.h')[:1]:
    shutil.copy(cfg, os.path.join(INC32, 'SDL2'))
EXTRA32 = ['-m32', '-I' + INC32, '-I/usr/include/SDL2']
cmds = {}
for e in json.load(open(os.path.join(build, 'compile_commands.json'))):
    f = os.path.normpath(os.path.join(e['directory'], e['file']))
    if '/subprojects/' not in f:
        cmds.setdefault(f, e)

def compile_obj(path, extra, out):
    e = cmds[os.path.join(root, path)]
    args, skip, cmd = shlex.split(e['command']), False, []
    for a in args:
        if skip: skip = False; continue
        if a in ('-o', '-MF', '-MQ', '-MT'): skip = True; continue
        if a in ('-c', '-MD', '-MMD') or a.startswith('-O') or a.startswith('-fsanitize') or a.endswith('.c'): continue
        cmd.append(a)
    cmd += ['-I' + os.path.join(root, 'include/pch')] + extra + ['-g', '-O0', '-c', '-w', '-o', out, os.path.join(root, path)]
    r = subprocess.run(cmd, cwd=e['directory'], capture_output=True, text=True)
    if r.returncode: sys.exit(path + ': ' + r.stderr[-500:])

layouts = {}
for kind, extra in (('port', []), ('ds', EXTRA32)):
    layouts[kind] = {}
    byfile = {}
    for t, p in CHUNKS.items(): byfile.setdefault(p, []).append(t)
    for p, types in byfile.items():
        obj = os.path.join(outdir, kind + '_' + os.path.basename(p) + '.o')
        compile_obj(p, extra, obj)
        js = obj + '.json'
        env = dict(os.environ, LAYOUT_TYPES=','.join(types), LAYOUT_OUT=js)
        subprocess.run(['gdb', '-batch', '-nx', '-x', os.path.join(S, 'dump_layout.py'), obj], env=env, capture_output=True)
        layouts[kind].update(json.load(open(js)))
json.dump(layouts, open(os.path.join(outdir, 'layouts.json'), 'w'))

def walk(d, prefix, base, out):  # flat list of (path, offset, size) for leaves and bitfields
    if d['kind'] == 'struct':
        for f in d['fields']:
            off = base + f['bitpos'] // 8
            if f['type']['kind'] == 'bits':
                out.append((prefix + '.' + f['name'] + ':bits', f['bitpos'] + base * 8, f['bitsize']))
            else:
                walk(f['type'], prefix + '.' + (f['name'] or '?'), off, out)
    elif d['kind'] == 'array':
        es = d['elem']['size']
        n = d['count']
        for i in range(n if d['elem']['kind'] != 'leaf' else 1):
            walk(d['elem'], '%s[%d]' % (prefix, i), base + i * es, out)
        if d['elem']['kind'] == 'leaf':
            out.append((prefix + '[]', base, d['size']))
    else:
        out.append((prefix, base, d['size']))

for t in CHUNKS:
    a, b = layouts['port'][t], layouts['ds'][t]
    fa, fb = [], []
    walk(a, t, 0, fa); walk(b, t, 0, fb)
    diffs = [(x, y) for x, y in zip(fa, fb) if x != y]
    status = 'same' if a['size'] == b['size'] and not diffs and len(fa) == len(fb) else 'DIFFERS'
    print('%-24s port %6d  ds %6d  %s' % (t, a['size'], b['size'], status))
    for x, y in diffs[:4]:
        print('    port %-60s @%d (%d)   ds %s @%d (%d)' % (x[0], x[1], x[2], y[0], y[1], y[2]))
