"""Generate the 60-floor Endgame Descent overlay for a loose D2R mod."""

import argparse
import csv
import io
import json
import re
from dataclasses import dataclass
from pathlib import Path

from stair_assets import HARROGATH_HD_TOWN, HARROGATH_TOWN, harrogath_door, harrogath_hover_light, lava_presets


EXCEL = Path("global/excel")
STRINGS = Path("local/lng/strings")
MONSTER_MODELS = Path("hd/character/monsters.json")

FLOOR_COUNT = 60
FIRST_AREA = 138
FIRST_AREA_LEVEL = 85
ENTRANCE_AREA = FIRST_AREA + FLOOR_COUNT
ENTRANCE_KEY = "GrailerDescentEntrance"
HARROGATH_AREA = 109
HARROGATH_DOOR_WARP = 83
BAAL_QUEST = 40

# Stock population templates, entry warp, exit Vis slot, exit warp, room counts.
SECTIONS = (
    ((35, 36), 17, 1, 18, (10, 12, 14)),
    ((30, 29, 31), 13, 1, 14, (11, 12, 14)),
    ((21, 22, 23, 24, 19, 18), 8, 1, 9, (14, 16, 18)),
    ((10, 11, 12, 9), 4, 4, 5, (6, 8, 10)),
    ((113, 115, 118, 114, 119), 73, 1, 74, (10, 12, 15)),
    ((125, 126, 127), 70, 1, 70, (3,)),
)
PREVIOUS_BOSSES = {
    "griswold": "griswold", "smith": "smith", "radament": "radament",
    "temptress": "succubus7", "andariel": "andariel", "duriel": "duriel",
    "mephisto": "mephisto", "diablo": "diablo", "baal": "baalcrab",
}
NEW_BOSSES = {
    "bloodraven": "bloodraven", "summoner": "summoner", "izual": "izual",
    "deseis": "doomknight3", "infector": "megademon3", "vizier": "fingermage3",
    "lister": "baalminion1",
}
BOSSES = PREVIOUS_BOSSES | NEW_BOSSES
COUNCIL = {
    "ismail": ("councilmember1", "Ismail Vilehand"),
    "geleb": ("councilmember2", "Geleb Flamefinger"),
    "toorc": ("councilmember1", "Toorc Icefist"),
    "bremm": ("councilmember3", "Bremm Sparkfist"),
    "wyand": ("councilmember2", "Wyand Voidfinger"),
    "maffer": ("councilmember3", "Maffer Dragonhand"),
}
BOSS_NAME_KEYS = {
    "deseis": "Lord De Seis", "infector": "Infector of Souls",
    "vizier": "Grand Vizier of Chaos", "lister": "Baal Subject 5",
}
QUEST_BOSS_COPIES = {"bloodraven", "summoner", "izual"}
ORIGINAL_BOSSES = ("griswold", "smith", "radament")
NAMES_PER_BOSS = 3
CHECKPOINTS = range(10, FLOOR_COUNT + 1, 10)
BONUS_GROUP = 27183
# These slots are shared with the DLL. Skip any items missing from the target data.
BONUS_ITEM_CODES = ("xa1", "xa2", "xa3", "xa4", "xa5", "csz", "pk1", "pk2", "pk3")
GEAR_AND_GEMS_SLOT = len(BONUS_ITEM_CODES) + 1
CHECKPOINT_RUNE_DIVISOR = 3
FIRST_STRING_ID = 64320
NAME_FORMATS = {
    "enUS": "Descent - Floor {}", "zhTW": "深淵之行 - 第{}層",
    "deDE": "Abstieg - Ebene {}", "esES": "Descenso - Nivel {}",
    "frFR": "Descente - Niveau {}", "itIT": "Discesa - Livello {}",
    "koKR": "심연 강하 - {}층", "plPL": "Zejście - Poziom {}",
    "esMX": "Descenso - Nivel {}", "jaJP": "深淵への降下 - 第{}層",
    "ptBR": "Descida - Nível {}", "ruRU": "Спуск - Уровень {}",
    "zhCN": "深渊之行 - 第{}层",
}
ENTRANCE_NAMES = {
    "enUS": "Descent Entrance", "zhTW": "深淵入口", "deDE": "Eingang zum Abstieg",
    "esES": "Entrada al descenso", "frFR": "Entrée de la descente", "itIT": "Ingresso della discesa",
    "koKR": "심연 입구", "plPL": "Wejście do podziemi", "esMX": "Entrada al descenso",
    "jaJP": "深淵への入口", "ptBR": "Entrada da descida", "ruRU": "Вход в подземелье", "zhCN": "深渊入口",
}
ECHO_NAME = (
    "Echo of {}", "{}的迴響", "Echo von {}", "Eco de {}", "Écho de {}", "Eco di {}",
    "{}의 메아리", "Echo postaci {}", "Eco de {}", "{}の残響", "Eco de {}", "Эхо — {}", "{}的回响",
)
SHADE_NAME = (
    "Shade of {}", "{}的暗影", "Schatten von {}", "Sombra de {}", "Ombre de {}", "Ombra di {}",
    "{}의 그림자", "Cień postaci {}", "Sombra de {}", "{}の影", "Sombra de {}", "Тень — {}", "{}的暗影",
)
TEMPTRESS_NAME = (
    "The Cinder Siren", "灰燼女妖", "Die Aschensirene", "La sirena de ceniza", "La Sirène des cendres",
    "La Sirena di Cenere", "잿불 세이렌", "Popielna Syrena", "La sirena de ceniza", "灰燼のセイレーン",
    "A Sereia das Cinzas", "Пепельная сирена", "灰烬女妖",
)


@dataclass(frozen=True)
class GameData:
    stock: Path
    mod: Path
    stock_excel: Path

    def stock_file(self, relative):
        path = self.stock_excel / relative.name if relative.parent == EXCEL else self.stock / relative
        if not path.is_file():
            raise ValueError(f"Required original game file is missing: {path}")
        return path

    def file(self, relative):
        path = self.mod / relative
        return path if path.is_file() else self.stock_file(relative)


def area_id(floor):
    return FIRST_AREA + floor - 1


def boss_id(floor, boss, name_index):
    base = f"descent_{boss}" if floor == 10 else f"descent{floor}_{boss}"
    return f"{base}_{name_index + 1}" if name_index else base


def boss_name_key(boss, name_index):
    return f"GrailerDescentBoss_{boss}_{name_index + 1}"


def boss_variants():
    # Preserve the class IDs of every variant from the previous builds.
    for floor in CHECKPOINTS:
        for boss in ORIGINAL_BOSSES:
            yield floor, boss, 0
    for floor in CHECKPOINTS:
        for boss in PREVIOUS_BOSSES:
            for name_index in range(NAMES_PER_BOSS):
                if name_index or boss not in ORIGINAL_BOSSES:
                    yield floor, boss, name_index
    for floor in CHECKPOINTS:
        for boss in NEW_BOSSES:
            for name_index in range(NAMES_PER_BOSS):
                yield floor, boss, name_index
        for boss in COUNCIL:
            yield floor, boss, 0


TABLE_KEYS = {
    "levels.txt": "Id", "lvlmaze.txt": "Level", "lvlprest.txt": "Def", "lvlwarp.txt": "Id",
    "monstats.txt": "Id", "monlvl.txt": "Level", "treasureclassex.txt": "Treasure Class",
    "armor.txt": "code", "weapons.txt": "code", "misc.txt": "code",
}
MONSTER_SCALING_COLUMNS = {
    f"{prefix}{stat}{difficulty}"
    for stat in ("AC", "TH", "HP", "DM", "XP")
    for prefix in ("", "L-")
    for difficulty in ("", "(N)", "(H)")
}


def validate_header(path, columns):
    if not columns or "" in columns:
        raise ValueError(f"{path} has an empty column name")
    if len(columns) != len(set(columns)):
        raise ValueError(f"{path} has duplicate column names")
    key = TABLE_KEYS[path.name]
    if key not in columns:
        raise ValueError(f"{path} is missing required key column {key}")
    return key


def table(path):
    raw = path.read_bytes()
    reader = csv.DictReader(io.StringIO(raw.decode("utf-8-sig")), delimiter="\t")
    validate_header(path, reader.fieldnames)
    rows = []
    for row in reader:
        if None in row:
            raise ValueError(f"{path} has a row with more fields than its header")
        rows.append({name: value if value is not None else "" for name, value in row.items()})
    return raw, rows


def append_rows(raw, rows, path):
    columns = next(csv.reader(io.StringIO(raw.decode("utf-8-sig")), delimiter="\t"), [])
    key = validate_header(path, columns)
    result = io.StringIO(newline="")
    writer = csv.DictWriter(result, columns, delimiter="\t", lineterminator="\r\n")
    active_names = set(columns)
    for row in rows:
        if "" in row or key not in row:
            raise ValueError(f"{path} has an empty source column name or a source row missing {key}")
        for name, value in row.items():
            if name != "*Creator" and name not in active_names and value not in (None, ""):
                raise ValueError(f"{path} is missing active column {name} for a nonempty Descent source value")
        mapped = {name: row.get(name, "") for name in columns}
        if "*Creator" in mapped:
            mapped["*Creator"] = "MadMike"
        writer.writerow(mapped)
    separator = b"" if raw.endswith(b"\n") else b"\r\n"
    return raw + separator + result.getvalue().encode("utf-8")


def write(output, relative, data):
    path = output / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def clear_population(level):
    for column in level:
        if re.fullmatch(r"(?:[nuc]?mon|cpct|camt)\d+", column):
            level[column] = ""
        if column.startswith(("MonDen", "MonUMin", "MonUMax", "ObjGrp", "ObjPrb")):
            level[column] = 0
    level.update(NumMon=0, rangedspawn=0)


def write_monster_scaling(data, output):
    relative = EXCEL / "monlvl.txt"
    raw, rows = table(data.file(relative))
    levels = [int(row["Level"]) for row in rows]
    if len(levels) < 111 or levels != list(range(len(levels))):
        raise ValueError("monlvl.txt must have consecutive levels starting at 0 and extending through at least 110")
    last_monster_level = FIRST_AREA_LEVEL + FLOOR_COUNT - 1 + 3
    if levels[-1] >= last_monster_level:
        return

    # Level 110 is the stock Uber boss row. Preserve it and use the slope of
    # levels 99–109 to continue the curve without repeating the jump at 110.
    anchor = rows[-1]
    trend_start, trend_end = rows[99], rows[109]
    trend_length = 10
    growth = {
        column: int(trend_end[column]) - int(trend_start[column])
        for column in anchor if column in MONSTER_SCALING_COLUMNS
    }
    extended = []
    for level in range(len(rows), last_monster_level + 1):
        steps = level - levels[-1]
        row = {"Level": level}
        for column, increase in growth.items():
            row[column] = max(0, int(anchor[column]) + (increase * steps + trend_length // 2) // trend_length)
        extended.append(row)
    write(output, relative, append_rows(raw, extended, relative))


def treasure_class(name, drops, **settings):
    row = {"Treasure Class": name, "Picks": 1, "NoDrop": 0} | settings
    for index, (item, weight) in enumerate(drops, start=1):
        row[f"Item{index}"] = item
        row[f"Prob{index}"] = weight
    return row


def checkpoint_rune_pools():
    lem_to_gul = "Descent Lem to Gul (H)"
    lower_runes = (("r20", 72), ("r21", 58), ("r22", 46), ("r23", 36), ("r24", 30), ("r25", 26))
    return [
        treasure_class(lem_to_gul, lower_runes),
        treasure_class("Descent Boss High Runes (H)", (
            (lem_to_gul, sum(weight for _, weight in lower_runes)),
            ("r26", 24), ("r27", 18), ("r28", 14), ("r29", 11),
            ("r30", 8), ("r31", 6), ("r32", 4), ("r33", 2),
        )),
    ]


def checkpoint_rewards():
    high_equipment = "Descent Boss High Equipment (H)"
    high_runes = "Descent Boss High Runes (H)"
    rewards = [
        treasure_class(high_equipment, (
            ("weap81", 2), ("armo81", 1), ("weap84", 6),
            ("armo84", 3), ("weap87", 15), ("armo87", 8),
        )),
    ] + checkpoint_rune_pools()
    for checkpoint in range(1, FLOOR_COUNT // 10 + 1):
        scale = CHECKPOINT_RUNE_DIVISOR
        high_equipment_weight = 20 * checkpoint
        standard_rune_weight = 140
        # Scale the denominator, then move the removed rune weight to ordinary equipment.
        equipment_weight = (5000 - high_equipment_weight) * scale + (standard_rune_weight + checkpoint) * (scale - 1)
        depth = checkpoint - 1
        rewards.append(treasure_class(f"Descent Floor {checkpoint * 10} (H)", (
            ("Act 5 (H) Equip C", equipment_weight),
            (high_equipment, high_equipment_weight * scale),
            ("Jewelry C", 600 * scale),
            ("Flawless Gem", (200 + 12 * checkpoint) * scale),
            ("Perfect Gem", 10 * checkpoint * scale),
            ("Runes 17", standard_rune_weight),
            (high_runes, checkpoint),
        ), Picks=4 + depth // 4, Unique=650 + 50 * depth, Set=620 + 50 * depth,
            Rare=780 + 30 * depth, Magic=1024))
    return rewards


def council_reward(floor, checkpoint):
    total_weight = sum(int(checkpoint.get(f"Prob{i}") or 0) for i in range(1, 11))
    scale = CHECKPOINT_RUNE_DIVISOR
    # Six members get one pick each, with about four successful picks at players 1.
    return checkpoint | {
        "Treasure Class": f"Descent Council Floor {floor} (H)", "Picks": 1,
        "NoDrop": total_weight // (2 * scale) * scale,
    }


def depth_bonus_rewards(data, group):
    exceptional = "Descent Exceptional Bases (H)"
    elite = "Descent Elite Bases (H)"
    gear = "Descent Bonus Gear (H)"
    rewards = [treasure_class(gear, (
        ("Act 5 (H) Equip C", 4), ("Jewelry C", 1),
    ), Unique=800, Set=800, Rare=600)]
    item_rows = []
    for name in ("weapons.txt", "armor.txt"):
        _, rows = table(data.file(EXCEL / name))
        item_rows.extend(rows)
    for pool, column in ((exceptional, "ubercode"), (elite, "ultracode")):
        items = sorted(
            (row["code"], int(row["rarity"])) for row in item_rows
            if row["code"] and row["code"] == row[column]
            and row["spawnable"] == "1" and int(row["rarity"] or 0) > 0
        )
        if not items or len({code for code, _ in items}) != len(items):
            raise ValueError(f"armor.txt and weapons.txt must define distinct spawnable {column} bases")
        # Each tier is a weighted tree. Internal weights are the sum of their
        # descendants, so splitting a tier into rows preserves item rarities.
        layer = 0
        while len(items) > 10:
            layer += 1
            parents = []
            for start in range(0, len(items), 10):
                chunk = items[start:start + 10]
                name = f"{pool.removesuffix(' (H)')} {layer}.{start // 10 + 1} (H)"
                rewards.append(treasure_class(name, chunk))
                parents.append((name, sum(weight for _, weight in chunk)))
            items = parents
        rewards.append(treasure_class(pool, items))
    gear_and_gems = (
        (exceptional, 330), (elite, 330), (gear, 180),
        ("Flawless Gem", 110), ("Perfect Gem", 44),
    )
    rewards.append(treasure_class("Descent Gear and Gems (H)", gear_and_gems,
                                  group=group + GEAR_AND_GEMS_SLOT, level=GEAR_AND_GEMS_SLOT + 1))
    rewards.append(treasure_class("Descent Depth Bonus (H)", gear_and_gems + (("Descent Boss High Runes (H)", 6),),
                                  group=group, level=1))
    return rewards


def bonus_item_rewards(data, group):
    available = set()
    for name in ("misc.txt", "weapons.txt", "armor.txt"):
        _, rows = table(data.file(EXCEL / name))
        available.update(row["code"].strip() for row in rows)
    # The DLL locates these one-item classes immediately after the ordinary bonus row.
    return [
        treasure_class(f"Descent Bonus {code} (H)", ((code, 1),), group=group + slot, level=slot + 1)
        for slot, code in enumerate(BONUS_ITEM_CODES, start=1) if code in available
    ]


def write_checkpoint_bosses(data, output):
    relative = EXCEL / "monstats.txt"
    raw, rows = table(data.file(relative))
    by_name = {row["Id"]: row for row in rows}
    first_index = 1 + max(int(row["*hcIdx"]) for row in rows if row["*hcIdx"].isdigit())
    variants = []
    for floor, boss, name_index in boss_variants():
        ident = boss_id(floor, boss, name_index)
        level = FIRST_AREA_LEVEL + floor - 1
        council = boss in COUNCIL
        reward = f"Descent Council Floor {floor} (H)" if council else f"Descent Floor {floor} (H)"
        if ident in by_name:
            raise ValueError(f"monstats.txt already defines the Descent monster {ident}")
        model, native_name = COUNCIL[boss] if council else (BOSSES[boss], BOSS_NAME_KEYS.get(boss))
        if model not in by_name:
            raise ValueError(f"monstats.txt is missing the Descent boss template {model}")
        base = by_name[model]
        name_key = boss_name_key(boss, name_index) if name_index or boss == "temptress" else native_name or base["NameStr"]
        variant = base | {
            "Id": ident, "*hcIdx": first_index + len(variants), "NextInClass": "",
            "NameStr": name_key, "Level(H)": level,
            "MinHP(H)": 250 if council else 600, "MaxHP(H)": 300 if council else 650,
            "Exp(H)": 120 if council else 315,
            "boss": 1, "primeevil": "", "isSpawn": 1, "minion1": "", "minion2": "", "PartyMin": "", "PartyMax": "",
            "TCQuestId": "", "TCQuestCP": "",
        }
        if boss in QUEST_BOSS_COPIES:
            variant["BaseId"] = ident
        for column in variant:
            if column.startswith("TreasureClassQuest"):
                variant[column] = ""
            elif column.startswith("TreasureClass") and column.endswith("(H)"):
                variant[column] = reward
        variants.append(variant)
    write(output, relative, append_rows(raw, variants, relative) if variants else raw)


def write_harrogath_entrance(data, output):
    relative = EXCEL / "levels.txt"
    raw = (output / relative).read_bytes()
    lines = raw.splitlines(keepends=True)
    columns = lines[0].decode("utf-8-sig").rstrip("\r\n").split("\t")
    id_column = columns.index("Id")
    changes = {
        HARROGATH_AREA: {"Vis0": ("0", FIRST_AREA),
                         "Warp0": ("-1", HARROGATH_DOOR_WARP)},
        FIRST_AREA: {"Vis0": (str(ENTRANCE_AREA), HARROGATH_AREA)},
    }
    for area in range(FIRST_AREA, ENTRANCE_AREA + 1):
        changes.setdefault(area, {}).update(QuestFlag=("", BAAL_QUEST), QuestFlagEx=("", BAAL_QUEST))
    for index, line in enumerate(lines[1:], start=1):
        row = line.rstrip(b"\r\n").split(b"\t")
        if len(row) <= id_column or not row[id_column].isdigit():
            continue
        for column, (expected, value) in changes.pop(int(row[id_column]), {}).items():
            cell = columns.index(column)
            updated = str(value).encode()
            if row[cell] != expected.encode():
                raise ValueError(f"levels.txt area {int(row[id_column])} has an occupied or unexpected {column}: {row[cell].decode()!r}")
            row[cell] = updated
        lines[index] = b"\t".join(row) + line[len(line.rstrip(b"\r\n")):]
    if changes:
        raise ValueError(f"Missing entrance or dungeon levels: {sorted(changes)}")
    write(output, relative, b"".join(lines))

    relative = EXCEL / "lvlwarp.txt"
    raw, rows = table(data.file(relative))
    warp = {
        "Name": "Harrogath to Descent", "Id": HARROGATH_DOOR_WARP, "UniqueId": 88,
        "SelectX": -20, "SelectY": -111, "SelectDX": 46, "SelectDY": 112,
        "ExitWalkX": 0, "ExitWalkY": 5, "OffsetX": 2, "OffsetY": 1,
        "LitVersion": 1, "Tiles": 2, "NoInteract": 0, "Direction": "b",
    }
    if any(row["Id"] == str(warp["Id"]) or row["UniqueId"] == str(warp["UniqueId"]) for row in rows):
        raise ValueError("lvlwarp.txt already uses Descent warp ID 83 or unique ID 88")
    write(output, relative, append_rows(raw, [warp], relative))

    write(output, HARROGATH_TOWN, harrogath_door(data.file(HARROGATH_TOWN).read_bytes()))
    write(output, HARROGATH_HD_TOWN, harrogath_hover_light(data.file(HARROGATH_HD_TOWN).read_bytes()))
    # A loose town preset takes precedence when the combined DS1 cache is disabled.
    cache = HARROGATH_TOWN.parent.parent / "combined_ds1.bin"
    if (data.mod / cache).is_file() and (data.mod / cache).stat().st_size:
        raise ValueError(f"The mod uses {cache}; export its custom presets as loose DS1 files before installing")
    write(output, cache, b"")


def write_tables(data, output):
    _, rows = table(data.stock_file(EXCEL / "levels.txt"))
    by_id = {int(row["Id"]): row for row in rows if row["Id"].isdigit()}
    layer_start = 100
    raw, active_levels = table(data.file(EXCEL / "levels.txt"))
    active_ids = [int(row["Id"]) for row in active_levels if row["Id"].isdigit()]
    if active_ids != list(range(FIRST_AREA)):
        raise ValueError("levels.txt must retain the original area rows 0–137 in order and leave IDs 138–198 free")
    for row in active_levels:
        if row["Id"].isdigit() and row["Layer"].isdigit() and int(row["Layer"]) >= layer_start:
            raise ValueError(f"levels.txt area {row['Id']} uses layer {row['Layer']}; layers 100 and up are reserved for Descent")
    _, maze_rows = table(data.stock_file(EXCEL / "lvlmaze.txt"))
    maze_raw, active_mazes = table(data.file(EXCEL / "lvlmaze.txt"))
    if any(row["Level"].isdigit() and FIRST_AREA <= int(row["Level"]) <= ENTRANCE_AREA for row in active_mazes):
        raise ValueError("lvlmaze.txt already uses Descent area IDs 138–198")
    stock_mazes = {int(row["Level"]): row for row in maze_rows if row["Level"].isdigit()}
    new_levels = []
    mazes = []
    for section, (templates, up_warp, down_slot, down_warp, room_counts) in enumerate(SECTIONS):
        for index in range(10):
            floor = 10 * section + index + 1
            area = area_id(floor)
            template = templates[index % len(templates)]
            rooms = room_counts[-1] if floor % 10 == 0 else room_counts[index % len(room_counts)]
            name = f"Descent {floor:02}"
            key = f"GrailerDescentFloor{floor:02}"
            level = by_id[template].copy()
            assert level["DrlgType"] == "1"
            grid_row, grid_column = divmod(floor - 1, 6)
            if grid_row % 2:
                grid_column = 5 - grid_column
            level.update({
                "Name": name, "*StringName": f"Descent - Floor {floor}",
                "Id": area, "Act": 4, "Layer": layer_start + floor - 1,
                "OffsetX": 3300 + 400 * grid_column, "OffsetY": 3200 + 300 * grid_row,
                "QuestFlag": "", "QuestFlagEx": "", "Quest": 0,
                "Waypoint": 255, "Depend": 0, "Portal": 0, "SaveMonsters": 1,
                "Position": 0, "PreventTownPortal": 0,
                "MonLvlEx(H)": FIRST_AREA_LEVEL + floor - 1, "MonLvl(H)": FIRST_AREA_LEVEL + floor - 1,
                "LevelName": key, "LevelWarp": key, "LevelEntry": key, "LevelGroup": "Null",
                "WarpDist": 500,
            })
            for slot in range(8):
                level.update({f"Vis{slot}": 0, f"Warp{slot}": -1})
            level.update(Vis0=ENTRANCE_AREA if floor == 1 else area - 1, Warp0=up_warp)
            if floor < FLOOR_COUNT:
                level.update({f"Vis{down_slot}": area + 1, f"Warp{down_slot}": down_warp})
            # Keep each level's bounds large enough for long maze seeds.
            size = 280 if section in (3, 4) else 200
            for axis in "XY":
                for difficulty in ("", "(N)", "(H)"):
                    level[f"Size{axis}{difficulty}"] = size
            new_levels.append(level)
            # The Frozen River and Icy Cellar use oversized special rooms.
            # Their monster pools can populate a normal 16x16 ice maze.
            maze_template = 113 if section == 4 else template
            mazes.append(stock_mazes[maze_template] | {
                "Name": name, "Level": area,
                "Rooms": rooms, "Rooms(N)": rooms, "Rooms(H)": rooms,
            })

    entrance = new_levels[0].copy()
    entrance.update({
        "Name": "Descent Entrance", "*StringName": ENTRANCE_NAMES["enUS"], "Id": ENTRANCE_AREA,
        "Layer": layer_start + FLOOR_COUNT, "OffsetX": 3300, "OffsetY": 2800, "DrlgType": 2,
        "Vis0": 0, "Warp0": -1, "Vis1": FIRST_AREA, "Warp1": 18,
        "SizeX": 12, "SizeY": 12, "SizeX(N)": 12, "SizeY(N)": 12, "SizeX(H)": 12, "SizeY(H)": 12,
    })
    entrance.update(LevelName=ENTRANCE_KEY, LevelWarp=ENTRANCE_KEY, LevelEntry=ENTRANCE_KEY, Position=0)
    clear_population(entrance)
    write(output, EXCEL / "levels.txt", append_rows(raw, new_levels + [entrance], EXCEL / "levels.txt"))
    write(output, EXCEL / "lvlmaze.txt", append_rows(maze_raw, mazes, EXCEL / "lvlmaze.txt"))

    _, rows = table(data.stock_file(EXCEL / "lvlprest.txt"))
    by_def = {int(row["Def"]): row for row in rows if row["Def"].isdigit()}
    raw, active_presets = table(data.file(EXCEL / "lvlprest.txt"))
    active_defs = [int(row["Def"]) for row in active_presets if row["Def"].isdigit()]
    if active_defs != list(range(1092)):
        raise ValueError("lvlprest.txt must retain the original preset rows 0 through 1091 in order and leave IDs 1092 through 1096 free")
    custom_presets = [by_def[294] | {
        "Name": "Descent Entrance", "Def": 1092, "LevelId": ENTRANCE_AREA, "Populate": 0,
    }, *lava_presets(by_def, data.stock, output, write)]
    write(output, EXCEL / "lvlprest.txt", append_rows(raw, custom_presets, EXCEL / "lvlprest.txt"))

    write_harrogath_entrance(data, output)
    write_checkpoint_bosses(data, output)

    raw, rows = table(data.file(EXCEL / "treasureclassex.txt"))
    checkpoints = checkpoint_rewards()
    floor_rewards = checkpoints[-len(CHECKPOINTS):]
    used_groups = {int(row["group"]) for row in rows if row["group"].isdigit()}
    group = next((start for start in range(BONUS_GROUP, 32768 - GEAR_AND_GEMS_SLOT)
                  if used_groups.isdisjoint(range(start, start + GEAR_AND_GEMS_SLOT + 1))), None)
    if group is None:
        raise ValueError("treasureclassex.txt has no free group range for the Descent bonus classes")
    rewards = checkpoints[:-len(CHECKPOINTS)] + depth_bonus_rewards(data, group)
    rewards += bonus_item_rewards(data, group)
    for floor, row in zip(CHECKPOINTS, floor_rewards, strict=True):
        rewards.extend((row, council_reward(floor, row)))
    reward_names = {row["Treasure Class"] for row in rewards}
    source_names = {row["Treasure Class"] for row in rows}
    referenced_pools = {"Act 5 (H) Equip C", "Jewelry C", "Flawless Gem", "Perfect Gem", "Runes 17"}
    if missing := referenced_pools - source_names:
        raise ValueError(f"treasureclassex.txt is missing required pools: {', '.join(sorted(missing))}")
    if collisions := reward_names & source_names:
        raise ValueError(f"treasureclassex.txt already uses these Descent names: {', '.join(sorted(collisions))}")
    write(output, EXCEL / "treasureclassex.txt", append_rows(raw, rewards, EXCEL / "treasureclassex.txt"))
    write_monster_scaling(data, output)


def boss_localizations(data):
    relative = EXCEL / "monstats.txt"
    _, stock_stats = table(data.stock_file(relative))
    _, active_stats = table(data.file(relative))
    stock_keys = {row["Id"]: row["NameStr"] for row in stock_stats}
    active_keys = {row["Id"]: row["NameStr"] for row in active_stats}
    names = {}
    for directory in (data.stock, data.mod):
        path = directory / STRINGS / "monsters.json"
        if path.is_file():
            for row in json.loads(path.read_text(encoding="utf-8-sig")):
                names.setdefault(row["Key"], {}).update(row)
    localized = {}
    for boss, model in BOSSES.items():
        if boss == "temptress":
            name = dict(zip(NAME_FORMATS, TEMPTRESS_NAME, strict=True))
            localized[boss_name_key(boss, 0)] = name
        else:
            stock_key = BOSS_NAME_KEYS.get(boss, stock_keys[model])
            name = names[stock_key] | names.get(BOSS_NAME_KEYS.get(boss, active_keys[model]), {})
        for name_index, templates in enumerate((ECHO_NAME, SHADE_NAME), start=1):
            localized[boss_name_key(boss, name_index)] = {
                locale: template.format(name[locale])
                for locale, template in zip(NAME_FORMATS, templates, strict=True)
            }
    return localized


def write_strings(data, output, translations):
    relative = STRINGS / "ui.json"
    raw = data.file(relative).read_bytes()
    entries = json.loads(raw.decode("utf-8-sig"))
    new_entries = []
    known = set()
    for directory in (data.stock / STRINGS, data.mod / STRINGS):
        for path in directory.glob("*.json"):
            contents = path.read_bytes()
            known.update(int(i) for i in re.findall(rb'"id"\s*:\s*(\d+)', contents))
            keys = {key.decode() for key in re.findall(rb'"Key"\s*:\s*"(GrailerDescent[\w]*)"', contents)}
            if collisions := keys & translations.keys():
                raise ValueError(f"{path} already defines {', '.join(sorted(collisions))}")
    id_order = (*range(FIRST_STRING_ID, 65535), *range(FIRST_STRING_ID - 1, -1, -1))
    free_ids = [string_id for string_id in id_order if string_id not in known]
    if len(free_ids) < len(translations):
        raise ValueError("Not enough unused localization IDs for Descent")
    for string_id, (name, localized_names) in zip(free_ids, translations.items()):
        assert localized_names.keys() == NAME_FORMATS.keys()
        new_entries.append({"id": string_id, "Key": name} | localized_names)
    suffix = json.dumps(new_entries, indent=2, ensure_ascii=False).replace("\n", "\r\n").encode("utf-8")
    closing_bracket = raw.rindex(b"]")
    separator = b"," if entries else b""
    localized = raw[:closing_bracket].rstrip() + separator + suffix[1:] + raw[closing_bracket + 1:]
    assert json.loads(localized.decode("utf-8-sig")) == entries + new_entries
    write(output, relative, localized)


def localize(data, output):
    translations = {
        f"GrailerDescentFloor{floor:02}": {locale: text.format(floor) for locale, text in NAME_FORMATS.items()}
        for floor in range(1, FLOOR_COUNT + 1)
    } | {ENTRANCE_KEY: ENTRANCE_NAMES} | boss_localizations(data)
    write_strings(data, output, translations)


def write_monster_models(data, output):
    raw = data.file(MONSTER_MODELS).read_bytes()
    models = json.loads(raw.decode("utf-8-sig"))
    stock_models = json.loads(data.stock_file(MONSTER_MODELS).read_text(encoding="utf-8-sig"))
    aliases = {}
    for floor, boss, name_index in sorted(boss_variants()):
        ident = boss_id(floor, boss, name_index)
        source_id = COUNCIL[boss][0] if boss in COUNCIL else BOSSES[boss]
        model = models.get(source_id, stock_models[source_id])
        if ident in models:
            raise ValueError(f"monsters.json already defines the Descent model {ident}")
        else:
            aliases[ident] = model
    closing_brace = raw.rindex(b"}")
    newline = b"\r\n" if b"\r\n" in raw else b"\n"
    additions = json.dumps(aliases, indent=2).encode("utf-8").replace(b"\n", newline)
    separator = b"," if models else b""
    updated = raw[:closing_brace].rstrip() + separator + additions[1:] + raw[closing_brace + 1:]
    assert json.loads(updated.decode("utf-8-sig")) == models | aliases
    write(output, MONSTER_MODELS, updated)


def build_overlay(data, output):
    write_tables(data, output)
    localize(data, output)
    write_monster_models(data, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-data", type=Path, required=True, help="Unpatched mod data directory")
    parser.add_argument("--stock-data", type=Path, required=True, help="Extracted base-game data directory")
    parser.add_argument("--stock-excel", type=Path, help="Override the stock global/excel directory")
    parser.add_argument("--output", type=Path, required=True, help="Staging directory for the data overlay")
    args = parser.parse_args()
    build_overlay(GameData(args.stock_data, args.source_data, args.stock_excel or args.stock_data / EXCEL), args.output)
    print(f"Staged the 60-floor dungeon at {args.output}")
