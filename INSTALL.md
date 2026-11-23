# Install Endgame Descent

Endgame Descent installs into the mod you select in D2RLoader. It works with a new mod or an existing compatible mod and appends dungeon rows after its existing data. The audited setup is Diablo II: Resurrected 3.4.95032 with D2RLoader 1.3.1-beta. Playing the dungeon requires an offline Reign of the Warlock character who has defeated Hell Baal.

## 1. Choose a mod

In the paths below, `<game>` is the folder that contains `D2RLoader.exe` and `<mod>` is the name of your mod's folder.

- **Existing mod.** Use an unpacked, writable mod. `<game>\mods\<mod>\<mod>.mpq` should be a folder containing `modinfo.json`. If you have a packed MPQ or compiled Excel `.bin` files in the mod's `data\global\excel`, obtain the mod's unpacked, editable TXT version before continuing.
- **New mod.** In D2RLoader, enable **Developer Console** under **Settings → General → Modding Tools**. At the main menu, press **Ctrl + backtick** and run `d2rl init MyMod`, replacing `MyMod` with your chosen name. [D2RLoader mod author tools](https://d2rloader.net/docs.html#7-mod-author-tools)

## 2. Copy the DLL

Close D2RLoader. Download the [Endgame Descent ZIP](https://github.com/MadMike616/d2rl-endgame-descent/releases/latest). In the ZIP, open `d2rloader\plugins` and copy `d2rl-endgame-descent.dll` into the folder below. Create `d2rloader\plugins` inside the mod if needed.

```text
<game>\mods\<mod>\d2rloader\plugins\d2rl-endgame-descent.dll
```

## Updating an existing Descent installation

Close D2RLoader, back up the installed DLL, and replace it with the new DLL from step 2. Keep the already installed dungeon data. Relaunch the same mod and check the version in **Extensions**.

## 3. Extract and copy the files your mod needs

With D2RLoader closed, check the table below against the files in `<game>\mods\<mod>\<mod>.mpq\data`. Keep the mod's existing TXT, JSON, and Harrogath files. Extract the original game files for every entry the mod is missing. The eight `lava*.ds1` and `lava*.json` files must come from the original game because the dungeon uses their stock layouts.

1. Download and unpack **Ladik’s CASC Viewer (CascView)**, then run the 64-bit executable. Click **Open Storage** and select the Diablo II: Resurrected installation folder that contains `D2R.exe`. [Ladik’s CASC Viewer download](https://www.zezula.net/en/casc/main.html)
2. Expand `data` in the storage tree and open its nested `data` folder, which contains `global`, `hd`, and `local`. Select the files you need from the table below and click **Extract**. CascView saves extracted files in its `Work` folder by default. For tables, select the Reign of the Warlock TXT files directly under `global\excel`.
3. Create the folder below. Copy the missing inputs and the eight stock lava files into it, keeping the paths in the table. For example, the extracted `data\hd\character\monsters.json` goes into `basefiles\hd\character\monsters.json`.

```text
<game>\mods\<mod>\d2rloader\plugins\basefiles-and-backups\basefiles\
```

| Folder under `basefiles` | Files |
|---|---|
| `global\excel` | `levels.txt`, `lvlmaze.txt`, `lvlprest.txt`, `lvlwarp.txt`, `monstats.txt`, `monlvl.txt`, `treasureclassex.txt`, `armor.txt`, `weapons.txt`, `misc.txt` |
| `local\lng\strings` | `ui.json` |
| `hd\character` | `monsters.json` |
| `global\tiles\expansion\town` | `townwest.ds1` |
| `hd\env\preset\expansion\town` | `townwest.json` |
| `global\tiles\act4\expansion` | `lavae.ds1`, `lavan.ds1`, `lavas.ds1`, `lavaw.ds1` |
| `hd\env\preset\act4\expansion` | `lavae.json`, `lavan.json`, `lavas.json`, `lavaw.json` |

If your mod starts with no game data, copy all 22 files in the table.

## 4. Run setup and check its log

1. Launch `D2RLoader.exe`. In **Settings → General**, set **Default Mod** to your chosen mod and enable **Allow Mod Extensions**. Apply and restart if you changed either setting. [D2RLoader settings](https://d2rloader.net/docs.html#general-settings)
2. Open `<game>\mods\<mod>\d2rloader\logs\endgame-descent.log` and look for `Endgame Descent setup complete`. If it reports a missing file, close D2RLoader and copy the extracted original into the `basefiles` folder from step 3, keeping its relative path. Launch the same mod again. The installer reads the originals from `basefiles` and writes the loose files it needs into `<mod>.mpq\data`. If the log is absent, confirm the selected mod and DLL location, then check the loader's **Extensions** tab.

## 5. Restart and enter the dungeon

1. Exit and relaunch D2RLoader with the same mod. Confirm that **Endgame Descent** appears in **Extensions** with the version you installed.
2. Start offline **Hell** with a **Reign of the Warlock** character who has completed the Hell Baal quest.
3. In Harrogath, click the wooden door beside the main gate to enter floor 1. Floor 1's up stairs return to the door.

## Compatibility and rollback

Existing custom area, preset, and warp IDs are supported. Setup needs the stock maze and doorway geometry, the usual Reign of the Warlock table structure, and one free Harrogath transition slot. It reports a conflict if the same physical door is already used, all eight slots are occupied, or a custom map cache needs to be exported as loose DS1 files. Use `endgame-descent.log` to identify the file involved.

Setup matches TXT columns by exact name, so the active mod can use a different column order. It checks for duplicate or empty header names, required key columns, and missing columns with values to transfer before writing any files. If the active table has a `*Creator` column, setup writes `MadMike` in that column on each new row.

Setup backs up edited originals in `<game>\mods\<mod>\d2rloader\plugins\basefiles-and-backups\backups\endgame-descent\<install>`. To undo the install, move any character in Descent back to town and close D2RLoader. Remove the DLL, restore the backed-up files to the same paths relative to `mods\<mod>`, and remove the files listed in `created-files.txt` in that backup. Keep a copy of any edits you made after installing so you can reapply them.
