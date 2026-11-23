#include "installer.h"
#include "dungeon_layout.h"

#include <D2RLPlugin/context.h>
#include <Windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
using namespace std::string_view_literals;

constexpr int InstallerResource = 210;
constexpr int DefaultWarp = 83;
constexpr int DefaultUniqueWarp = 88;
constexpr int LastTableId = UINT16_MAX;
constexpr int BonusGroup = 27183;
constexpr int GearSlot = 10;
constexpr auto Excel = L"global/excel";
constexpr auto Strings = L"local/lng/strings";
constexpr auto Models = L"hd/character/monsters.json";
constexpr auto Town = L"global/tiles/expansion/town/townwest.ds1";
constexpr auto TownHD = L"hd/env/preset/expansion/town/townwest.json";
constexpr auto TownCache = L"global/tiles/expansion/combined_ds1.bin";
constexpr auto Marker = L"endgame-descent-install-v1.txt";
constexpr auto InstallFiles = L"plugins/basefiles-and-backups";

std::string Utf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

bool Within(const fs::path& root, const fs::path& path) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && !relative.has_root_path() && *relative.begin() != L"..";
}

fs::path Checked(const fs::path& root, const fs::path& relative) {
    const auto path = fs::weakly_canonical(root / relative);
    if (!Within(root, path)) throw std::runtime_error("Path is outside its expected directory: " + Utf8(path));
    return path;
}

std::string Read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not read " + Utf8(path));
    return {std::istreambuf_iterator<char>(file), {}};
}

std::optional<std::string> ReadOptional(const fs::path& path) {
    if (!fs::exists(path)) return std::nullopt;
    return Read(path);
}

class InstallLock {
public:
    InstallLock() {
        handle = CreateMutexW(nullptr, FALSE, L"Local\\D2RLoader.ModFirstRunInstall.v1");
        if (!handle) throw std::runtime_error("Could not open the shared mod install lock.");
        const auto result = WaitForSingleObject(handle, 10000);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) return;
        CloseHandle(handle);
        throw std::runtime_error("Another mod install holds the shared install lock. Restart D2RLoader to retry.");
    }
    ~InstallLock() { ReleaseMutex(handle); CloseHandle(handle); }
    InstallLock(const InstallLock&) = delete;
    InstallLock& operator=(const InstallLock&) = delete;
private:
    HANDLE handle{};
};

struct Paths {
    fs::path package;
    fs::path loader;
    fs::path game;

    fs::path Target(const fs::path& relative) const { return Checked(package, L"data" / relative); }
    fs::path Stock(const fs::path& relative) const { return Checked(loader, fs::path(InstallFiles) / L"basefiles" / relative); }
};

Paths Locate(const D2RL::PluginContext& context) {
    if (!context.activeMod || !*context.activeMod || !context.pluginConfigPath || !*context.pluginConfigPath)
        throw std::runtime_error("Select a mod in D2RLoader before installing Endgame Descent.");
    const fs::path name(std::u8string_view(reinterpret_cast<const char8_t*>(context.activeMod)));
    if (name.has_parent_path() || name.has_root_path() || name == L"." || name == L"..")
        throw std::runtime_error("Invalid active mod name.");
    const auto loader = fs::weakly_canonical(fs::path(context.pluginConfigPath).parent_path().parent_path());
    const auto mod = loader.parent_path();
    if (_wcsicmp(loader.filename().c_str(), L"d2rloader") != 0 || _wcsicmp(mod.filename().c_str(), name.c_str()) != 0
        || _wcsicmp(mod.parent_path().filename().c_str(), L"mods") != 0)
        throw std::runtime_error("The active mod must use <game>/mods/<mod>/d2rloader for its plugins.");
    const auto expected = mod / (name.native() + L".mpq");
    const auto package = Checked(mod, expected.filename());
    if (!fs::is_directory(package) || !fs::is_regular_file(package / L"modinfo.json"))
        throw std::runtime_error("Use an unpacked mod with modinfo.json in " + Utf8(expected)
                                 + ". For a new mod, use d2rl init <mod_name>.");
    return {package, loader, mod.parent_path().parent_path()};
}

std::optional<int> Number(std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == value.npos) return std::nullopt;
    value = value.substr(first, value.find_last_not_of(" \t") - first + 1);
    int number{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (error == std::errc{} && end == value.data() + value.size()) return number;
    return std::nullopt;
}

std::vector<std::string> Split(std::string_view text, char separator) {
    std::vector<std::string> fields;
    for (;;) {
        const auto end = text.find(separator);
        fields.emplace_back(text.substr(0, end));
        if (end == text.npos) return fields;
        text.remove_prefix(end + 1);
    }
}

std::string_view TableKey(const fs::path& file) {
    const auto name = file.filename();
    if (name == L"levels.txt" || name == L"lvlwarp.txt" || name == L"monstats.txt") return "Id";
    if (name == L"lvlmaze.txt" || name == L"monlvl.txt") return "Level";
    if (name == L"lvlprest.txt") return "Def";
    if (name == L"treasureclassex.txt") return "Treasure Class";
    if (name == L"armor.txt" || name == L"weapons.txt" || name == L"misc.txt") return "code";
    throw std::logic_error("No table key defined for " + Utf8(file));
}

struct Tsv {
    using Row = std::vector<std::string>;

    Tsv(std::string bytes, const fs::path& file) : key(TableKey(file)) {
        const auto eol = bytes.find('\n');
        newline = eol != bytes.npos && eol != 0 && bytes[eol - 1] == '\r' ? "\r\n" : "\n";
        header = bytes.substr(0, eol);
        if (header.ends_with('\r')) header.pop_back();
        bytes.erase(0, eol == bytes.npos ? bytes.size() : eol + 1);
        columns = Split(header.starts_with("\xef\xbb\xbf") ? std::string_view(header).substr(3) : header, '\t');
        std::set<std::string_view> names;
        for (const auto& name : columns) {
            if (name.empty()) throw std::runtime_error(Utf8(file) + " has an empty column name.");
            if (!names.insert(name).second)
                throw std::runtime_error(Utf8(file) + " has a duplicate column named " + name + ".");
        }
        if (!names.contains(key))
            throw std::runtime_error(Utf8(file) + " is missing required key column " + std::string(key) + ".");

        std::string_view remaining(bytes);
        while (!remaining.empty()) {
            const auto end = remaining.find('\n');
            auto line = remaining.substr(0, end);
            const bool crlf = line.ends_with('\r');
            if (crlf) line.remove_suffix(1);
            auto row = Split(line, '\t');
            if (row.size() > columns.size())
                throw std::runtime_error(Utf8(file) + " has a row with more fields than its header.");
            rows.push_back(std::move(row));
            rowEndings.emplace_back(end == remaining.npos ? "" : crlf ? "\r\n" : "\n");
            if (end == remaining.npos) break;
            remaining.remove_prefix(end + 1);
        }
    }

    std::size_t Column(std::string_view name) const {
        const auto i = std::ranges::find(columns, name);
        if (i == columns.end()) throw std::runtime_error("Missing table column " + std::string(name));
        return static_cast<std::size_t>(i - columns.begin());
    }

    std::string_view Get(const Row& row, std::string_view name) const {
        const auto i = Column(name);
        return i < row.size() ? std::string_view(row[i]) : std::string_view{};
    }

    void Set(Row& row, std::string_view name, std::string value) const {
        const auto i = Column(name);
        if (row.size() <= i) row.resize(i + 1);
        row[i] = std::move(value);
    }

    // A supplied template is copied from this same table and already uses its column order.
    void Add(const Json& values, Row row = {}) {
        if (!values.is_object() || !values.contains(key))
            throw std::runtime_error("Descent source row is missing key column " + std::string(key) + ".");
        row.resize(columns.size());
        for (const auto& [name, field] : values.items()) {
            if (name.empty()) throw std::runtime_error("Descent source row has an empty column name.");
            if (name == "*Creator") continue;
            const auto& value = field.get_ref<const std::string&>();
            const auto column = std::ranges::find(columns, name);
            if (column != columns.end()) row[column - columns.begin()] = value;
            else if (!value.empty())
                throw std::runtime_error("Missing active column " + name + " for a nonempty Descent source value.");
        }
        const auto creator = std::ranges::find(columns, "*Creator");
        if (creator != columns.end()) row[creator - columns.begin()] = "MadMike";
        rows.push_back(std::move(row));
    }

    std::string Serialize() const {
        auto bytes = header + newline;
        for (std::size_t index = 0; index < rows.size(); ++index) {
            const auto& row = rows[index];
            for (std::size_t i = 0; i < row.size(); ++i) {
                if (i) bytes += '\t';
                bytes += row[i];
            }
            if (index < rowEndings.size() && (!rowEndings[index].empty() || index + 1 == rows.size())) bytes += rowEndings[index];
            else bytes += newline;
        }
        return bytes;
    }

    std::string header, newline;
    Row columns;
    std::vector<Row> rows;
    std::vector<std::string_view> rowEndings;

private:
    std::string_view key;
};

Json Parse(const std::string& bytes) {
    // D2RLoader accepts comments and trailing commas in localized strings.
    auto parsed = Json::parse(bytes, nullptr, false, true);
    if (!parsed.is_discarded()) return parsed;
    std::string cleaned;
    cleaned.reserve(bytes.size());
    bool quoted = false;
    std::size_t previous = std::string::npos;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const char ch = bytes[i];
        if (!quoted && ch == '/' && i + 1 < bytes.size()) {
            if (bytes[i + 1] == '/') {
                const auto end = bytes.find('\n', i + 2);
                if (end == bytes.npos) break;
                i = end - 1;
                continue;
            }
            if (bytes[i + 1] == '*') {
                const auto end = bytes.find("*/", i + 2);
                if (end == bytes.npos) throw std::runtime_error("Unclosed JSON block comment.");
                cleaned += ' ';
                i = end + 1;
                continue;
            }
        }
        if (!quoted && (ch == ']' || ch == '}') && previous != cleaned.npos && cleaned[previous] == ',')
            cleaned[previous] = ' ';
        cleaned += ch;
        if (quoted && ch == '\\' && i + 1 < bytes.size()) { cleaned += bytes[++i]; continue; }
        if (ch == '"') quoted = !quoted;
        if (!quoted && ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') previous = cleaned.size() - 1;
    }
    return Json::parse(cleaned);
}

Json Seed() {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&Seed), &module))
        throw std::runtime_error("Could not locate the Descent installer resource.");
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(InstallerResource), MAKEINTRESOURCEW(10));
    if (!resource) throw std::runtime_error("The DLL is missing its Descent installer data.");
    const auto* bytes = static_cast<const char*>(LockResource(LoadResource(module, resource)));
    if (!bytes) throw std::runtime_error("Could not read the Descent installer resource.");
    return Json::parse(bytes, bytes + SizeofResource(module, resource));
}

struct Edit {
    fs::path target;
    std::optional<std::string> original;
    std::string value;
};

fs::path ExtendedWritePath(fs::path path) {
    path.make_preferred();
    const auto& name = path.native();
    if (name.starts_with(L"\\\\?\\")) return path;
    if (name.starts_with(L"\\\\")) return L"\\\\?\\UNC\\" + name.substr(2);
    return L"\\\\?\\" + name;
}

void WriteAtomic(const fs::path& target, const std::string& bytes, bool replace) {
    // The backup path preserves each mod-relative filename and can exceed MAX_PATH.
    const auto destination = ExtendedWritePath(target);
    fs::create_directories(destination.parent_path());
    auto temporary = destination;
    temporary += L".descent-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file || !MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw std::runtime_error("Could not write " + Utf8(target));
    }
}

void Apply(const Paths& paths, const std::vector<Edit>& edits) {
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    const auto timestamp = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    const auto backup = Checked(paths.loader, fs::path(InstallFiles) / L"backups/endgame-descent" /
                                (std::to_wstring(timestamp) + L"-" + std::to_wstring(GetCurrentProcessId())));
    fs::create_directories(backup);
    std::string created;
    for (const auto& edit : edits) {
        const auto relative = edit.target.lexically_relative(paths.package.parent_path());
        if (edit.original) WriteAtomic(Checked(backup, relative), *edit.original, false);
        else created += Utf8(relative) + '\n';
    }
    WriteAtomic(backup / L"created-files.txt", created, false);

    std::vector<const Edit*> applied;
    applied.reserve(edits.size());
    try {
        for (const auto& edit : edits) {
            if (ReadOptional(edit.target) != edit.original)
                throw std::runtime_error("File changed during setup: " + Utf8(edit.target));
            WriteAtomic(edit.target, edit.value, edit.original.has_value());
            applied.push_back(&edit);
        }
    } catch (...) {
        bool restored = true;
        for (const auto* edit : std::views::reverse(applied)) {
            try {
                if (ReadOptional(edit->target) != edit->value) restored = false;
                else if (edit->original) WriteAtomic(edit->target, *edit->original, true);
                else fs::remove(edit->target);
            } catch (...) { restored = false; }
        }
        if (!restored) throw std::runtime_error("Rollback was incomplete. Restore the affected files from " + Utf8(backup));
        throw;
    }
}

using Inputs = std::map<fs::path, fs::path>;

std::vector<fs::path> CompilerCaches(const Paths& paths) {
    std::vector<fs::path> caches;
    // Only use the shared game cache here. A mod's compiler output may have
    // materialized changes that belong to that mod's previous build.
    const auto compiler = paths.game / L"d2rloader/data/compiler";
    if (!fs::is_directory(compiler)) return caches;
    std::map<fs::path, fs::file_time_type> timestamps;
    for (const auto& entry : fs::directory_iterator(compiler)) {
        if (!entry.is_directory() || !fs::is_regular_file(entry.path() / L"manifest.json")) continue;
        caches.push_back(entry.path());
        timestamps.emplace(entry.path(), fs::last_write_time(entry.path() / L"manifest.json"));
    }
    std::ranges::sort(caches, std::greater<>{}, [&](const fs::path& cache) { return timestamps.at(cache); });
    for (auto& cache : caches) cache /= L"data/global/excel";
    return caches;
}

Inputs FindInputs(const Paths& paths, const Json& seed, const D2RL::PluginContext& context) {
    const auto caches = CompilerCaches(paths);
    Inputs inputs;
    bool missing = false;
    const auto require = [&](const fs::path& relative, bool useMod, bool allowBase) {
        const auto target = paths.Target(relative);
        const auto stock = paths.Stock(relative);
        std::vector<fs::path> candidates;
        if (useMod) candidates.push_back(target);
        candidates.push_back(stock);
        if (relative.parent_path() == Excel) {
            for (const auto& cache : caches) {
                candidates.push_back(cache / relative.filename());
                if (allowBase) candidates.push_back(cache / L"base" / relative.filename());
            }
        }
        candidates.push_back(paths.game / L"Data" / relative);
        for (const auto& candidate : candidates) {
            if (fs::is_regular_file(candidate)) { inputs.emplace(relative, candidate); return; }
        }
        missing = true;
        const auto message = "Missing data/" + Utf8(relative) + ". Extract the original file from your D2R installation and put it in "
                           + Utf8(stock) + (useMod ? ". The mod's own loose file can also be placed in " + Utf8(target) : "") + ".";
        context.LogError(message.c_str());
    };

    for (const auto* filename : {L"levels.txt", L"lvlmaze.txt", L"lvlprest.txt", L"lvlwarp.txt", L"monstats.txt",
                                 L"monlvl.txt", L"treasureclassex.txt", L"armor.txt", L"weapons.txt", L"misc.txt"}) {
        const auto relative = fs::path(Excel) / filename;
        // The base subfolder excludes RotW-only areas and treasure classes.
        require(relative, true, relative.filename() != L"levels.txt" && relative.filename() != L"treasureclassex.txt");
    }
    require(fs::path(Strings) / L"ui.json", true, false);
    require(Models, true, false);
    require(Town, true, false);
    require(TownHD, true, false);
    for (const auto& stair : seed.at("lava")) {
        const fs::path clone(stair[0].get<std::string>());
        auto original = clone.parent_path() / clone.filename().wstring().substr(8);
        require(L"global/tiles" / original, false, false);
        require(L"hd/env/preset" / original.replace_extension(L".json"), false, false);
    }
    if (missing) throw std::runtime_error("Required files are missing. See the paths above and restart D2RLoader after adding them.");
    return inputs;
}

int NextRowId(const Tsv& table, std::string_view key, const char* filename) {
    // The game indexes these compiled tables by row number.
    int next = 0;
    for (const auto& row : table.rows) {
        if (const auto id = Number(table.Get(row, key))) {
            if (*id != next)
                throw std::runtime_error(std::string(filename) + " has ID " + std::to_string(*id)
                                         + " at native row " + std::to_string(next) + ". Use consecutive IDs in row order.");
            ++next;
        }
    }
    return next;
}

std::size_t AreaRowIndex(const Tsv& table, std::uint32_t id) {
    const auto row = std::ranges::find_if(table.rows, [&](const Tsv::Row& fields) { return Number(table.Get(fields, "Id")) == static_cast<int>(id); });
    if (row == table.rows.end()) throw std::runtime_error("levels.txt is missing area ID " + std::to_string(id) + ".");
    return static_cast<std::size_t>(row - table.rows.begin());
}

std::string Cell(std::string_view name, std::uint32_t slot) {
    return std::string(name) + std::to_string(slot);
}

std::pair<int, int> NextWarpIds(const Tsv& table, const Tsv& levels) {
    int id = DefaultWarp - 1;
    int unique = DefaultUniqueWarp - 1;
    for (const auto& row : table.rows) {
        id = std::max(id, Number(table.Get(row, "Id")).value_or(-1));
        unique = std::max(unique, Number(table.Get(row, "UniqueId")).value_or(-1));
    }
    for (const auto& row : levels.rows) {
        for (std::uint32_t slot = 0; slot < 8; ++slot)
            id = std::max(id, Number(levels.Get(row, Cell("Warp", slot))).value_or(-1));
    }
    if (id >= LastTableId - 1 || unique >= LastTableId - 1)
        throw std::runtime_error("lvlwarp.txt has no free numeric IDs for the Descent entrance.");
    return {id + 1, unique + 1};
}

DungeonPlacement AllocatePlacement(const Tsv& levels, const Tsv& presets, int warp) {
    const int area = NextRowId(levels, "Id", "levels.txt");
    const int preset = NextRowId(presets, "Def", "lvlprest.txt");
    if (area < static_cast<int>(DefaultDungeonArea) || preset < static_cast<int>(DefaultDungeonPreset))
        throw std::runtime_error("Use the complete Reign of the Warlock levels.txt and lvlprest.txt as the base for Descent.");
    std::set<int> layers;
    for (const auto& row : levels.rows) {
        if (const auto layer = Number(levels.Get(row, "Layer"))) layers.insert(*layer);
    }
    int layer = DefaultDungeonLayer;
    for (const auto used : layers) {
        if (used < layer) continue;
        if (used > LastTableId || used - layer > static_cast<int>(DungeonFloorCount)) break;
        layer = used + 1;
    }
    if (area > LastTableId - static_cast<int>(DungeonFloorCount) || preset > LastTableId - 4
        || layer > LastTableId - static_cast<int>(DungeonFloorCount))
        throw std::runtime_error("The mod has reached the area, automap layer, or preset ID range needed for Descent.");
    return {static_cast<std::uint32_t>(area), static_cast<std::uint32_t>(layer),
            static_cast<std::uint32_t>(preset + 1), static_cast<std::uint32_t>(warp), 0};
}

std::string MergeLevels(Tsv table, const Json& seed, const DungeonPlacement& placement) {
    auto& row = table.rows[AreaRowIndex(table, HarrogathArea)];
    table.Set(row, Cell("Vis", placement.doorSlot), std::to_string(placement.firstArea));
    table.Set(row, Cell("Warp", placement.doorSlot), std::to_string(placement.doorWarp));
    for (auto floor : seed.at("levels")) {
        const auto index = std::stoul(floor.at("Id").get<std::string>()) - DefaultDungeonArea;
        floor["Id"] = std::to_string(placement.firstArea + index);
        floor["Layer"] = std::to_string(placement.firstLayer + index);
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const auto key = Cell("Vis", slot);
            const auto target = std::stoul(floor.at(key).get<std::string>());
            if (target >= DefaultDungeonArea && target <= DefaultDungeonArea + DungeonFloorCount)
                floor[key] = std::to_string(placement.firstArea + target - DefaultDungeonArea);
        }
        table.Add(floor);
    }
    return table.Serialize();
}

std::string MergeMazes(Tsv table, const Json& seed, const DungeonPlacement& placement) {
    for (const auto& row : table.rows) {
        if (Number(table.Get(row, "Level")).value_or(0) >= static_cast<int>(placement.firstArea))
            throw std::runtime_error("lvlmaze.txt refers to an area without a matching row in levels.txt.");
    }
    for (auto floor : seed.at("mazes")) {
        const auto id = std::stoul(floor.at("Level").get<std::string>());
        floor["Level"] = std::to_string(placement.firstArea + id - DefaultDungeonArea);
        table.Add(floor);
    }
    return table.Serialize();
}

std::string MergePresets(Tsv table, const Json& seed, const DungeonPlacement& placement) {
    for (const auto& row : table.rows) {
        if (Number(table.Get(row, "LevelId")).value_or(0) >= static_cast<int>(placement.firstArea))
            throw std::runtime_error("lvlprest.txt refers to an area without a matching row in levels.txt.");
    }
    auto def = placement.firstLavaPreset - 1;
    for (auto preset : seed.at("presets")) {
        if (def == placement.firstLavaPreset - 1) preset["LevelId"] = std::to_string(placement.firstArea + DungeonFloorCount);
        preset["Def"] = std::to_string(def++);
        table.Add(preset);
    }
    return table.Serialize();
}

std::string MergeWarps(Tsv table, Json warp, int id, int unique) {
    warp["Id"] = std::to_string(id);
    warp["UniqueId"] = std::to_string(unique);
    table.Add(warp);
    return table.Serialize();
}

std::string MergeBosses(Tsv table, const Json& seed) {
    std::map<std::string, std::size_t, std::less<>> ids;
    int last = -1;
    for (std::size_t i = 0; i < table.rows.size(); ++i) {
        const auto& row = table.rows[i];
        ids.emplace(table.Get(row, "Id"), i);
        last = std::max(last, Number(table.Get(row, "*hcIdx")).value_or(-1));
    }
    if (last < 0 || last + static_cast<int>(seed.at("bosses").size()) > 32767)
        throw std::runtime_error("monstats.txt has no valid free monster indices for Descent bosses.");
    for (const auto& boss : seed.at("bosses")) {
        const auto source = boss[0].get<std::string>();
        const auto& patch = boss[1];
        const auto id = patch.at("Id").get<std::string>();
        if (ids.contains(id)) throw std::runtime_error("monstats.txt already defines " + id);
        if (!ids.contains(source)) throw std::runtime_error("monstats.txt is missing the boss template " + source);
        auto row = table.rows[ids.at(source)];
        table.Set(row, "*hcIdx", std::to_string(++last));
        table.Add(patch, std::move(row));
    }
    return table.Serialize();
}

std::string MergeScaling(Tsv table) {
    const int anchor = NextRowId(table, "Level", "monlvl.txt") - 1;
    if (anchor < 110) throw std::runtime_error("monlvl.txt must extend through at least monster level 110.");
    std::vector<Tsv::Row> scalingRows;
    for (const auto& row : table.rows) if (Number(table.Get(row, "Level"))) scalingRows.push_back(row);
    constexpr std::string_view statColumns[]{
        "AC", "AC(N)", "AC(H)", "L-AC", "L-AC(N)", "L-AC(H)",
        "TH", "TH(N)", "TH(H)", "L-TH", "L-TH(N)", "L-TH(H)",
        "HP", "HP(N)", "HP(H)", "L-HP", "L-HP(N)", "L-HP(H)",
        "DM", "DM(N)", "DM(H)", "L-DM", "L-DM(N)", "L-DM(H)",
        "XP", "XP(N)", "XP(H)", "L-XP", "L-XP(N)", "L-XP(H)",
    };
    const auto& stats = scalingRows.back();
    for (int level = anchor + 1; level <= 147; ++level) {
        Json row{{"Level", std::to_string(level)}};
        for (const auto name : statColumns) {
            if (std::ranges::find(table.columns, name) == table.columns.end()) continue;
            const auto start = Number(table.Get(scalingRows[99], name));
            const auto end = Number(table.Get(scalingRows[109], name));
            const auto value = Number(table.Get(stats, name));
            if (!start || !end || !value) throw std::runtime_error("monlvl.txt has invalid values in " + std::string(name));
            const auto scaled = static_cast<std::int64_t>(*end - static_cast<std::int64_t>(*start)) * (level - anchor) + 5;
            const auto increment = (scaled < 0 ? scaled - 9 : scaled) / 10;
            row[name] = std::to_string(std::max<std::int64_t>(0, *value + increment));
        }
        table.Add(row);
    }
    return table.Serialize();
}

using Drop = std::pair<std::string, int>;

Json TreasureClass(std::string_view name, std::span<const Drop> drops) {
    Json row{{"Treasure Class", name}, {"Picks", "1"}, {"NoDrop", "0"}};
    for (std::size_t i = 0; i < drops.size(); ++i) {
        const auto slot = std::to_string(i + 1);
        row["Item" + slot] = drops[i].first;
        row["Prob" + slot] = std::to_string(drops[i].second);
    }
    return row;
}

void AddBasePool(std::vector<Json>& rewards, std::vector<Drop> items, const std::string& pool) {
    if (items.empty()) throw std::runtime_error("No spawnable items found for " + pool);
    std::ranges::sort(items);
    if (std::ranges::adjacent_find(items, {}, &Drop::first) != items.end())
        throw std::runtime_error("Duplicate item codes in " + pool);
    for (int layer = 1; items.size() > 10; ++layer) {
        std::vector<Drop> parents;
        for (std::size_t i = 0; i < items.size(); i += 10) {
            const auto chunk = std::span<const Drop>(items).subspan(i, std::min<std::size_t>(10, items.size() - i));
            const auto name = pool + " " + std::to_string(layer) + "." + std::to_string(i / 10 + 1) + " (H)";
            std::int64_t weight = 0;
            for (const auto& drop : chunk) weight += drop.second;
            if (weight > INT_MAX) throw std::runtime_error("Total item rarity is too high in " + pool);
            rewards.push_back(TreasureClass(name, chunk));
            parents.emplace_back(name, static_cast<int>(weight));
        }
        items = std::move(parents);
    }
    rewards.push_back(TreasureClass(pool + " (H)", items));
}

std::string MergeTreasure(Tsv table, const Json& seed, const Inputs& inputs) {
    std::set<std::string, std::less<>> names;
    std::set<int> groups;
    for (const auto& row : table.rows) {
        names.emplace(table.Get(row, "Treasure Class"));
        if (const auto group = Number(table.Get(row, "group"))) groups.insert(*group);
    }
    for (const auto* name : {"Act 5 (H) Equip C", "Jewelry C", "Flawless Gem", "Perfect Gem", "Runes 17"})
        if (!names.contains(name)) throw std::runtime_error("treasureclassex.txt is missing required pool " + std::string(name));
    int group = BonusGroup;
    for (; group <= 32657; ++group) {
        const auto taken = groups.lower_bound(group);
        if (taken == groups.end() || *taken > group + GearSlot) break;
    }
    if (group > 32657) throw std::runtime_error("No free treasure class groups for Descent bonus loot.");

    std::set<std::string, std::less<>> codes;
    std::vector<Drop> exceptional, elite;
    for (const auto* filename : {L"armor.txt", L"weapons.txt", L"misc.txt"}) {
        const auto& input = inputs.at(fs::path(Excel) / filename);
        const auto items = Tsv(Read(input), input);
        for (const auto& row : items.rows) {
            const auto code = items.Get(row, "code");
            if (code.empty()) continue;
            codes.emplace(code);
            if (std::wstring_view(filename) == L"misc.txt") continue;
            const auto rarity = Number(items.Get(row, "rarity")).value_or(0);
            if (rarity <= 0 || items.Get(row, "spawnable") != "1") continue;
            if (items.Get(row, "ubercode") == code) exceptional.emplace_back(code, rarity);
            if (items.Get(row, "ultracode") == code) elite.emplace_back(code, rarity);
        }
    }
    for (int rune = 20; rune <= 33; ++rune) {
        const auto code = "r" + std::to_string(rune);
        if (!codes.contains(code)) throw std::runtime_error("The item tables are missing required rune code " + code);
    }
    std::vector<Json> rewards;
    for (auto row : seed.at("rewards")) {
        const auto name = row.at("Treasure Class").get<std::string>();
        if (name == "Descent Gear and Gems (H)") {
            AddBasePool(rewards, std::move(exceptional), "Descent Exceptional Bases");
            AddBasePool(rewards, std::move(elite), "Descent Elite Bases");
        }
        if (row.contains("group")) row["group"] = std::to_string(group + std::stoi(row.at("group").get<std::string>()) - BonusGroup);
        rewards.push_back(std::move(row));
        if (name != "Descent Depth Bonus (H)") continue;
        int slot = 0;
        for (const auto* code : {"xa1", "xa2", "xa3", "xa4", "xa5", "csz", "pk1", "pk2", "pk3"}) {
            ++slot;
            if (!codes.contains(code)) continue;
            const Drop drop{code, 1};
            auto special = TreasureClass("Descent Bonus " + std::string(code) + " (H)", std::span(&drop, 1));
            special["group"] = std::to_string(group + slot);
            special["level"] = std::to_string(slot + 1);
            rewards.push_back(std::move(special));
        }
    }
    for (const auto& row : rewards) {
        const auto name = row.at("Treasure Class").get<std::string>();
        if (!names.insert(name).second) throw std::runtime_error("treasureclassex.txt already defines " + name);
        table.Add(row);
    }
    return table.Serialize();
}

std::uint32_t Word(const std::string& bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) throw std::runtime_error("Truncated DS1 tile data.");
    std::uint32_t word{};
    std::memcpy(&word, bytes.data() + offset, sizeof word);
    return word;
}

void SetWord(std::string& bytes, std::size_t offset, std::uint32_t word) {
    Word(bytes, offset);
    std::memcpy(bytes.data() + offset, &word, sizeof word);
}

struct MapLayers { std::size_t start, width, height, walls; };

MapLayers ReadLayers(const std::string& bytes) {
    const auto version = Word(bytes, 0);
    const auto width = Word(bytes, 4);
    const auto height = Word(bytes, 8);
    if (version < 16 || version > 18 || width >= 256 || height >= 256)
        throw std::runtime_error("Unrecognized DS1 header.");
    const auto files = Word(bytes, 20);
    std::size_t start = 24;
    for (std::uint32_t i = 0; i < files; ++i) {
        start = bytes.find('\0', start);
        if (start == bytes.npos) throw std::runtime_error("Truncated DS1 file list.");
        ++start;
    }
    const auto walls = Word(bytes, start);
    if (walls == 0 || walls > 4) throw std::runtime_error("Unexpected DS1 wall layer count.");
    return {start + 8, width + 1, height + 1, walls};
}

std::size_t DoorCell(const std::string& bytes, const MapLayers& map) {
    if (map.width != 41 || map.height != 41 || map.walls != 4 || Word(bytes, 12) != 4)
        throw std::runtime_error("townwest.ds1 has a different Harrogath layout.");
    return map.start + 4 * (16 * map.width + 7);
}

void ChooseDoorSlot(DungeonPlacement& placement, const Tsv& levels, const std::string& bytes) {
    const auto map = ReadLayers(bytes);
    const auto cells = map.width * map.height;
    const auto door = DoorCell(bytes, map);
    std::uint32_t usedSlots = 0;
    for (std::size_t layer = 0; layer < map.walls; ++layer) {
        const auto wall = map.start + 8 * layer * cells;
        for (std::size_t index = 0; index < cells; ++index) {
            const auto tile = wall + 4 * index;
            const auto orientation = Word(bytes, tile + 4 * cells);
            if (orientation != 10 && orientation != 11) continue;
            if (index == (door - map.start) / 4)
                throw std::runtime_error("townwest.ds1 already has a transition at the Descent door tile (7, 16).");
            const auto slot = (Word(bytes, tile) >> 20) & 63;
            if (slot < 8) usedSlots |= 1U << slot;
        }
    }
    const auto& town = levels.rows[AreaRowIndex(levels, HarrogathArea)];
    for (std::uint32_t slot = 0; slot < 8; ++slot) {
        const auto vis = levels.Get(town, Cell("Vis", slot));
        const auto warp = levels.Get(town, Cell("Warp", slot));
        if ((usedSlots & (1U << slot)) == 0 && (vis.empty() || Number(vis) == 0)
            && (warp.empty() || Number(warp) == -1)) {
            placement.doorSlot = slot;
            return;
        }
    }
    throw std::runtime_error("Harrogath needs one free Vis/Warp slot in levels.txt for the Descent door. All eight are occupied.");
}

std::string MergeTown(std::string bytes, std::uint32_t slot) {
    const auto map = ReadLayers(bytes);
    const auto cell = DoorCell(bytes, map);
    const auto orientation = cell + 4 * map.width * map.height;
    if (Word(bytes, cell) || Word(bytes, orientation))
        throw std::runtime_error("townwest.ds1 already uses Harrogath's door tile at (7, 16).");
    SetWord(bytes, cell, 0x81 | (slot << 20));
    SetWord(bytes, orientation, 11);
    return bytes;
}

std::string MergeTownHD(Json town, Json light) {
    const Json* position = nullptr;
    auto& entities = town.at("entities");
    for (const auto& entity : entities) {
        if (entity.at("name") == light.at("name") || entity.at("id") == light.at("id"))
            throw std::runtime_error("townwest.json already uses the Descent hover light name or ID.");
        if (entity.at("name") != "act1_door03_01") continue;
        for (const auto& component : entity.at("components")) {
            if (component.at("type") != "TransformDefinitionComponent") continue;
            if (position) throw std::runtime_error("townwest.json defines more than one Harrogath door transform.");
            position = &component.at("position");
        }
    }
    if (!position) throw std::runtime_error("townwest.json is missing the act1_door03_01 Harrogath door transform.");
    light["components"][0]["position"] = Json{{"x", position->at("x").get<double>() - 1},
                                               {"y", position->at("y").get<double>() + 6},
                                               {"z", position->at("z").get<double>() + 4}};
    entities.push_back(std::move(light));
    return town.dump();
}

std::string LavaStair(std::string bytes, int slot, const Json& position) {
    const auto map = ReadLayers(bytes);
    const auto cells = map.width * map.height;
    const auto expected = position[1].get<std::size_t>() * map.width + position[0].get<std::size_t>();
    std::optional<std::size_t> marker;
    for (std::size_t layer = 0; layer < map.walls; ++layer) {
        const auto start = map.start + 8 * layer * cells;
        for (std::size_t cell = 0; cell < cells; ++cell) {
            const auto orientation = Word(bytes, start + 4 * (cells + cell));
            if (orientation != 10 && orientation != 11) continue;
            const auto offset = start + 4 * cell;
            if (marker || orientation != 10 || cell != expected || ((Word(bytes, offset) >> 20) & 63) != 33)
                throw std::runtime_error("Unexpected stock lava stair marker. Supply the original map for this game build.");
            marker = offset;
        }
    }
    if (!marker) throw std::runtime_error("Stock lava preset has no stair marker.");
    SetWord(bytes, *marker, 0x90000081 | (static_cast<std::uint32_t>(slot) << 20));
    return bytes;
}

std::string MergeStrings(const Paths& paths, const Inputs& inputs, const Json& seed) {
    const auto ui = inputs.at(fs::path(Strings) / L"ui.json");
    auto entries = Parse(Read(ui));
    if (!entries.is_array()) throw std::runtime_error("ui.json must contain an array of localized strings.");
    std::set<int> ids;
    std::set<std::string, std::less<>> keys;
    const auto index = [&](const Json& rows) {
        if (!rows.is_array()) return;
        for (const auto& row : rows) {
            if (!row.is_object()) continue;
            if (row.contains("id") && row["id"].is_number_integer()) ids.insert(row["id"].get<int>());
            if (row.contains("Key") && row["Key"].is_string()) keys.insert(row["Key"].get<std::string>());
        }
    };
    index(entries);
    for (const auto& directory : {paths.Stock(Strings), paths.game / L"Data" / Strings, paths.Target(Strings)}) {
        if (!fs::is_directory(directory)) continue;
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != L".json" || entry.path() == ui) continue;
            try { index(Parse(Read(entry.path()))); }
            catch (const std::exception& error) { throw std::runtime_error(Utf8(entry.path()) + ": " + error.what()); }
        }
    }
    int id = seed.at("firstStringId").get<int>();
    for (auto entry : seed.at("strings")) {
        const auto key = entry.at("Key").get<std::string>();
        if (keys.contains(key)) throw std::runtime_error("Localization already defines " + key);
        while (ids.contains(id)) ++id;
        if (id >= 65535) throw std::runtime_error("No free localization IDs for Descent in 64320–65534.");
        entry["id"] = id++;
        entries.push_back(std::move(entry));
    }
    return entries.dump(2) + '\n';
}

std::string MergeModels(Json models, const Json& seed) {
    std::vector<std::pair<std::string, std::string>> aliases;
    for (const auto& boss : seed.at("bosses")) {
        aliases.emplace_back(boss[1].at("Id").get<std::string>(), boss[0].get<std::string>());
    }
    // The unnumbered Descent IDs belong to floor 10. The model map is keyed by name.
    std::ranges::sort(aliases, {}, [](const auto& alias) {
        auto id = std::string_view(alias.first).substr("descent"sv.size());
        const auto separator = id.find('_');
        return std::pair{Number(id.substr(0, separator)).value_or(10), id.substr(separator + 1)};
    });
    for (const auto& [id, source] : aliases) {
        if (models.contains(id)) throw std::runtime_error("monsters.json already defines the model " + id);
        if (!models.contains(source)) throw std::runtime_error("monsters.json is missing the model " + source);
        auto model = models.at(source);
        models[id] = std::move(model);
    }
    return models.dump(2) + '\n';
}

bool CheckInstalled(const Paths& paths, const D2RL::PluginContext& context) {
    const auto relative = fs::path(Excel) / L"levels.txt";
    const auto marker = Checked(paths.loader, Marker);
    const auto levels = ReadOptional(paths.Target(relative));
    // Keep the original dungeon string keys so existing installs retain their rows and IDs.
    if (!fs::exists(marker) && (!levels || levels->find("GrailerDescentFloor01") == std::string::npos)) return false;
    bool missing = false;
    const auto require = [&](const fs::path& installed) {
        const auto path = paths.Target(installed);
        if (fs::is_regular_file(path)) return;
        missing = true;
        const auto message = "Installed Descent file is missing. Restore it to " + Utf8(path) + ".";
        context.LogError(message.c_str());
    };
    for (const auto* file : {L"levels.txt", L"lvlmaze.txt", L"lvlprest.txt", L"lvlwarp.txt", L"monstats.txt", L"monlvl.txt", L"treasureclassex.txt"})
        require(fs::path(Excel) / file);
    for (const auto* file : {Town, TownHD, TownCache, Models}) require(file);
    require(fs::path(Strings) / L"ui.json");
    for (const auto direction : L"ensw"sv) {
        const auto name = std::wstring(L"descent_lava") + direction;
        require(L"global/tiles/act4/expansion" / fs::path(name + L".ds1"));
        require(L"hd/env/preset/act4/expansion" / fs::path(name + L".json"));
    }
    if (missing) throw std::runtime_error("Installed Descent data is incomplete. See the paths above.");
    if (!levels) throw std::runtime_error("levels.txt changed during setup. Restart D2RLoader to retry.");
    const auto table = Tsv(*levels, paths.Target(relative));
    DungeonPlacement placement{};
    for (const auto& row : table.rows) {
        if (table.Get(row, "LevelName") != "GrailerDescentFloor01") continue;
        const auto area = Number(table.Get(row, "Id")).value_or(-1);
        const auto layer = Number(table.Get(row, "Layer")).value_or(-1);
        if (placement.firstArea != 0 || area < static_cast<int>(DefaultDungeonArea)
            || area > LastTableId - static_cast<int>(DungeonFloorCount)
            || layer < static_cast<int>(DefaultDungeonLayer) || layer > LastTableId - static_cast<int>(DungeonFloorCount))
            throw std::runtime_error("Invalid or duplicate Descent floor 1 in " + Utf8(paths.Target(relative)) + ".");
        placement.firstArea = static_cast<std::uint32_t>(area);
        placement.firstLayer = static_cast<std::uint32_t>(layer);
    }
    if (placement.firstArea == 0) throw std::runtime_error("Missing Descent floor 1 in " + Utf8(paths.Target(relative)) + ".");
    for (std::uint32_t index = 0; index <= DungeonFloorCount; ++index) {
        const auto& row = table.rows[AreaRowIndex(table, placement.firstArea + index)];
        const auto floor = std::to_string(index + 1);
        const auto key = index == DungeonFloorCount ? "GrailerDescentEntrance" : "GrailerDescentFloor" + std::string(index < 9 ? "0" : "") + floor;
        if (table.Get(row, "LevelName") != key || Number(table.Get(row, "Layer")) != static_cast<int>(placement.firstLayer + index))
            throw std::runtime_error("Incomplete Descent area rows in " + Utf8(paths.Target(relative)) + ".");
    }
    const auto townMap = Read(paths.Target(Town));
    const auto layers = ReadLayers(townMap);
    const auto cell = DoorCell(townMap, layers);
    const auto slot = (Word(townMap, cell) >> 20) & 63;
    if (slot >= 8 || Word(townMap, cell) != (0x81U | (slot << 20))
        || Word(townMap, cell + 4 * layers.width * layers.height) != 11)
        throw std::runtime_error("Descent door marker is incomplete in " + Utf8(paths.Target(Town)) + ".");
    const auto& town = table.rows[AreaRowIndex(table, HarrogathArea)];
    const auto warp = Number(table.Get(town, Cell("Warp", slot))).value_or(-1);
    if (warp < DefaultWarp || warp >= LastTableId || Number(table.Get(town, Cell("Vis", slot))) != static_cast<int>(placement.firstArea)
        || Number(table.Get(table.rows[AreaRowIndex(table, placement.firstArea)], "Vis0")) != static_cast<int>(HarrogathArea))
        throw std::runtime_error("Descent's Harrogath link is incomplete in " + Utf8(paths.Target(relative)) + ".");
    placement.doorWarp = static_cast<std::uint32_t>(warp);
    placement.doorSlot = slot;

    const auto presetsPath = paths.Target(fs::path(Excel) / L"lvlprest.txt");
    const auto presets = Tsv(Read(presetsPath), presetsPath);
    constexpr std::string_view names[]{"n", "s", "e", "w"};
    std::uint32_t found = 0;
    for (const auto& row : presets.rows) {
        const auto file = presets.Get(row, "File1");
        if (!file.starts_with("act4/expansion/descent_lava")) continue;
        const auto def = Number(presets.Get(row, "Def")).value_or(-1);
        if (found >= std::size(names) || file != "act4/expansion/descent_lava" + std::string(names[found]) + ".ds1"
            || def < static_cast<int>(DefaultDungeonPreset + 1) || def > LastTableId
            || (found > 0 && def != static_cast<int>(placement.firstLavaPreset + found)))
            throw std::runtime_error("Invalid Descent stair preset sequence in " + Utf8(presetsPath) + ".");
        if (found++ == 0) placement.firstLavaPreset = static_cast<std::uint32_t>(def);
    }
    if (found != std::size(names)) throw std::runtime_error("Missing Descent lava stair rows in " + Utf8(presetsPath) + ".");
    dungeonPlacement = placement;
    return true;
}

InstallResult Install(const Paths& paths, const D2RL::PluginContext& context) {
    const auto excel = paths.Target(Excel);
    if (fs::is_directory(excel)) {
        for (const auto& entry : fs::directory_iterator(excel)) {
            auto extension = entry.path().extension().wstring();
            for (auto& letter : extension) letter = static_cast<wchar_t>(std::towlower(letter));
            if (extension == L".bin") throw std::runtime_error("Found compiled Excel data at " + Utf8(entry.path())
                + ". Install on a copy of the mod with its complete editable TXT sources. Remove the compiled BIN set from that copy before retrying.");
        }
    }
    if (CheckInstalled(paths, context)) return InstallResult::Ready;
    const auto seed = Seed();
    const auto inputs = FindInputs(paths, seed, context);
    const auto levelFile = fs::path(Excel) / L"levels.txt";
    const auto presetFile = fs::path(Excel) / L"lvlprest.txt";
    const auto warpFile = fs::path(Excel) / L"lvlwarp.txt";
    const auto prepare = [&](const fs::path& relative) {
        auto target = paths.Target(relative);
        auto original = ReadOptional(target);
        auto input = original ? *original : Read(inputs.at(relative));
        return Edit{std::move(target), std::move(original), std::move(input)};
    };
    std::vector<Edit> edits;
    const auto stage = [&](Edit edit, const auto& make) {
        try {
            edit.value = make(edit.value);
        } catch (const std::exception& error) { throw std::runtime_error(Utf8(edit.target) + ": " + error.what()); }
        if (!edit.original || edit.value != *edit.original) edits.push_back(std::move(edit));
    };
    const auto merge = [&](const fs::path& relative, const auto& make) { stage(prepare(relative), make); };
    const auto mergeTable = [&](const wchar_t* name, const auto& make) {
        const auto relative = fs::path(Excel) / name;
        auto edit = prepare(relative);
        Tsv table(edit.value, inputs.at(relative));
        stage(std::move(edit), [&](const std::string&) { return make(std::move(table)); });
    };
    auto levelEdit = prepare(levelFile);
    auto presetEdit = prepare(presetFile);
    auto warpEdit = prepare(warpFile);
    auto townEdit = prepare(Town);
    Tsv levels(levelEdit.value, inputs.at(levelFile)), presets(presetEdit.value, inputs.at(presetFile)),
        warps(warpEdit.value, inputs.at(warpFile));
    const auto [warp, unique] = NextWarpIds(warps, levels);
    auto placement = AllocatePlacement(levels, presets, warp);
    try { ChooseDoorSlot(placement, levels, townEdit.value); }
    catch (const std::exception& error) { throw std::runtime_error(Utf8(inputs.at(Town)) + ": " + error.what()); }

    stage(std::move(levelEdit), [&](const std::string&) { return MergeLevels(std::move(levels), seed, placement); });
    stage(std::move(presetEdit), [&](const std::string&) { return MergePresets(std::move(presets), seed, placement); });
    stage(std::move(warpEdit), [&](const std::string&) { return MergeWarps(std::move(warps), seed.at("warp"), warp, unique); });
    stage(std::move(townEdit), [&](const std::string& bytes) { return MergeTown(bytes, placement.doorSlot); });
    mergeTable(L"lvlmaze.txt", [&](Tsv table) { return MergeMazes(std::move(table), seed, placement); });
    mergeTable(L"monstats.txt", [&](Tsv table) { return MergeBosses(std::move(table), seed); });
    mergeTable(L"monlvl.txt", MergeScaling);
    mergeTable(L"treasureclassex.txt", [&](Tsv table) { return MergeTreasure(std::move(table), seed, inputs); });
    merge(TownHD, [&](const std::string& bytes) { return MergeTownHD(Parse(bytes), seed.at("light")); });
    merge(fs::path(Strings) / L"ui.json", [&](const std::string&) { return MergeStrings(paths, inputs, seed); });
    merge(Models, [&](const std::string& bytes) { return MergeModels(Parse(bytes), seed); });

    const auto cache = paths.Target(TownCache);
    const auto previousCache = ReadOptional(cache);
    if (previousCache && !previousCache->empty())
        throw std::runtime_error("Custom map cache in " + Utf8(cache) + ". Export the custom DS1s as loose files before installing.");
    if (!previousCache) edits.push_back({cache, std::nullopt, {}});

    for (const auto& stair : seed.at("lava")) {
        const auto clone = fs::path(stair[0].get<std::string>());
        const auto original = clone.parent_path() / clone.filename().wstring().substr(8);
        const auto tile = paths.Target(L"global/tiles" / clone);
        const auto hd = paths.Target(L"hd/env/preset" / fs::path(clone).replace_extension(L".json"));
        for (const auto& path : {tile, hd}) {
            if (fs::exists(path)) throw std::runtime_error("Descent lava filename is already in use: " + Utf8(path));
        }
        const auto originalTile = inputs.at(L"global/tiles" / original);
        try {
            edits.push_back({tile, std::nullopt, LavaStair(Read(originalTile), stair[1].get<int>(), stair[2])});
        } catch (const std::exception& error) { throw std::runtime_error(Utf8(originalTile) + ": " + error.what()); }
        edits.push_back({hd, std::nullopt, Read(inputs.at(L"hd/env/preset" / fs::path(original).replace_extension(L".json")))});
    }
    edits.push_back({Checked(paths.loader, Marker), std::nullopt, "Endgame Descent install format 1\n"});
    Apply(paths, edits);
    dungeonPlacement = placement;
    return InstallResult::RestartRequired;
}
}

InstallResult InstallDescentData(const D2RL::PluginContext& context) noexcept {
    try {
        const auto paths = Locate(context);
        const InstallLock lock;
        const auto result = Install(paths, context);
        if (result == InstallResult::RestartRequired)
            context.LogInfo("Endgame Descent setup complete. Restart D2RLoader with the same mod to enter the dungeon.");
        return result;
    } catch (const std::exception& error) {
        const auto message = "Endgame Descent setup failed. " + std::string(error.what());
        context.LogError(message.c_str());
        return InstallResult::Failed;
    }
}
