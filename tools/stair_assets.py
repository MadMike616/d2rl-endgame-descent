"""Prepare native transitions for Harrogath and the Abaddon end rooms."""

import json
import struct
from pathlib import Path


# Native lava generation pairs north with south and east with west.
# The first stock file in each direction has a stair landing.
LAVA_STAIRS = (
    (1053, 1093, 0, (3, 18)),
    (1054, 1094, 1, (17, 4)),
    (1055, 1095, 0, (3, 4)),
    (1056, 1096, 1, (17, 17)),
)
HARROGATH_TOWN = Path("global/tiles/expansion/town/townwest.ds1")
HARROGATH_HD_TOWN = Path("hd/env/preset/expansion/town/townwest.json")


def wall_layers(data):
    version, width, height, _, _, files = struct.unpack_from("<6I", data)
    assert 16 <= version <= 18
    width += 1
    height += 1
    offset = 24
    for _ in range(files):
        offset = data.index(0, offset) + 1
    walls, _ = struct.unpack_from("<2I", data, offset)
    offset += 8
    return width, height, offset, walls


def harrogath_door(data):
    width, height, start, walls = wall_layers(data)
    if (width, height, walls) != (41, 41, 4) or struct.unpack_from("<I", data, 12)[0] != 4:
        raise ValueError("Unrecognized Harrogath west town preset")
    x, y = 7, 16
    tile = start + 4 * (y * width + x)
    orientation = tile + 4 * width * height
    original = struct.unpack_from("<I", data, tile)[0], struct.unpack_from("<I", data, orientation)[0]
    if original not in ((0, 0), (0x81, 11)):
        raise ValueError(f"Harrogath door cell is already in use: {original}")
    data = bytearray(data)
    # Right wall warp marker, style zero selects Harrogath's Vis0.
    struct.pack_into("<I", data, tile, 0x81)
    struct.pack_into("<I", data, orientation, 11)
    return bytes(data)


def harrogath_hover_light(raw):
    town = json.loads(raw)
    light_name = "descent_entrance_hover_light"
    light_id = 268915910
    door, = (entity for entity in town["entities"] if entity["name"] == "act1_door03_01")
    transform, = (component for component in door["components"]
                  if component["type"] == "TransformDefinitionComponent")
    position = transform["position"]
    # The door model's origin is at its base. Light the outside face and threshold.
    light = {
        "type": "Entity", "name": light_name, "id": light_id, "components": [
            {
                "type": "TransformDefinitionComponent", "name": light_name + "_transform",
                "position": {"x": position["x"] - 1.0, "y": position["y"] + 6.0, "z": position["z"] + 4.0},
                "orientation": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 1.0},
                "scale": {"x": 1.0, "y": 1.0, "z": 1.0}, "inheritOnlyPosition": False,
            },
            {
                "type": "PointLightDefinitionComponent", "name": light_name + "_point",
                "color": {"x": 1.0, "y": 0.88, "z": 0.68},
                # Match the identifying radius and attenuation in dungeon_entrance.cpp.
                "power": 0.000001, "radius": 14.375, "attenuation": 3.1875,
                "lightMask": 1, "isLocalLight": False,
                "diffuseContribution": 1.0, "specularContribution": 0.5,
            },
        ],
    }
    matches = [entity for entity in town["entities"] if entity["id"] == light_id or entity["name"] == light_name]
    if matches:
        if matches != [light]:
            raise ValueError("Harrogath hover light ID or name is already in use")
        return raw
    town["entities"].append(light)
    return json.dumps(town, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def special_tiles(data):
    width, height, offset, walls = wall_layers(data)
    cells = width * height
    for layer in range(walls):
        start = offset + 8 * layer * cells
        for index in range(cells):
            orientation, = struct.unpack_from("<I", data, start + 4 * (cells + index))
            if orientation in (10, 11):
                tile_offset = start + 4 * index
                flags, = struct.unpack_from("<I", data, tile_offset)
                yield (index % width, index // width), orientation, (flags >> 20) & 63, tile_offset


def lava_presets(stock, assets, output, write):
    for source_def, def_id, slot, position in LAVA_STAIRS:
        preset = stock[source_def].copy()
        assert preset["LevelId"] == "0" and preset["SizeX"] == preset["SizeY"] == "24"
        source = Path(preset["File1"].lower())
        target = source.with_name("descent_" + source.name)
        tile_root = Path("global/tiles")
        hd_root = Path("hd/env/preset")
        data = bytearray((assets / tile_root / source).read_bytes())
        (actual_position, orientation, marker, offset), = special_tiles(data)
        assert actual_position == position and orientation == 10 and marker == 33

        # Reuse the lava stair graphics and replace the special marker
        # with the stock WarpMesa flags and the destination's Vis slot.
        struct.pack_into("<I", data, offset, 0x90000081 | (slot << 20))
        assert list(special_tiles(data)) == [(position, 10, slot, offset)]
        write(output, tile_root / target, data)
        write(output, hd_root / target.with_suffix(".json"),
              (assets / hd_root / source.with_suffix(".json")).read_bytes())
        preset.update(Name=f"Descent Lava {source.stem[-1].upper()} Stair", Def=def_id, Files=1)
        for index in range(1, 7):
            key = f"File{index}"
            if key in preset:
                preset[key] = target.as_posix() if index == 1 else 0
        yield preset
