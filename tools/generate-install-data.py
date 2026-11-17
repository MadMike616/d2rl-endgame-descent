"""Build the embedded Descent data from the audited stock game files."""

import argparse
import json
from pathlib import Path

from build_prototype import (
    BONUS_GROUP, BOSSES, COUNCIL, EXCEL, FIRST_AREA, FIRST_STRING_ID, STRINGS,
    GameData, boss_id, boss_variants, build_overlay, table,
)
from stair_assets import HARROGATH_HD_TOWN, LAVA_STAIRS


def fields(row):
    return {key: value for key, value in row.items() if value and not key.startswith("*")}


def generate(stock, excel, output, stage):
    data = GameData(stock, stage / "no-overrides", excel)
    build_overlay(data, stage)

    def appended(filename):
        relative = EXCEL / filename
        _, original = table(data.stock_file(relative))
        _, generated = table(stage / relative)
        return generated[len(original):]

    result = {
        "levels": [row for row in appended("levels.txt") if int(row["Id"]) >= FIRST_AREA],
        "mazes": appended("lvlmaze.txt"),
        "presets": appended("lvlprest.txt"),
        "warp": appended("lvlwarp.txt")[0],
    }
    monsters = {row["Id"]: row for row in appended("monstats.txt")}
    result["bosses"] = []
    fixed = ("Id", "Level(H)", "MinHP(H)", "MaxHP(H)", "Exp(H)", "boss", "primeevil", "isSpawn",
             "minion1", "minion2", "PartyMin", "PartyMax", "NextInClass", "TCQuestId", "TCQuestCP")
    for floor, boss, index in boss_variants():
        row = monsters[boss_id(floor, boss, index)]
        model = COUNCIL[boss][0] if boss in COUNCIL else BOSSES[boss]
        patch = {name: row[name] for name in fixed}
        for name in row:
            if name.startswith("TreasureClassQuest") or name.startswith("TreasureClass") and name.endswith("(H)"):
                patch[name] = row[name]
        if row["BaseId"] == row["Id"]:
            patch["BaseId"] = row["BaseId"]
        if index or boss in {"temptress", "deseis", "infector", "vizier", "lister"} or boss in COUNCIL:
            patch["NameStr"] = row["NameStr"]
        result["bosses"].append([model, patch])

    rewards = appended("treasureclassex.txt")
    result["rewards"] = [fields(row) for row in rewards if not row["Treasure Class"].startswith((
        "Descent Exceptional Bases", "Descent Elite Bases", "Descent Bonus xa", "Descent Bonus pk",
    ))]
    assert any(row.get("group") == str(BONUS_GROUP) for row in result["rewards"])
    ui_path = STRINGS / "ui.json"
    with (stage / ui_path).open(encoding="utf-8-sig") as stream:
        result["strings"] = [entry for entry in json.load(stream) if entry["Key"].startswith("GrailerDescent")]
    with (stage / HARROGATH_HD_TOWN).open(encoding="utf-8-sig") as stream:
        result["light"] = next(entry for entry in json.load(stream)["entities"]
                               if entry["name"] == "descent_entrance_hover_light")
    result["lava"] = [[row["File1"], slot, list(position)]
                      for row, (_, _, slot, position) in zip(result["presets"][1:], LAVA_STAIRS, strict=True)]
    result["firstStringId"] = FIRST_STRING_ID
    output.write_text(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path)
    parser.add_argument("excel", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("stage", type=Path)
    arguments = parser.parse_args()
    generate(arguments.stock, arguments.excel, arguments.output, arguments.stage)
