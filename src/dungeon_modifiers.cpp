#include "dungeon_modifiers.h"

#include <algorithm>
#include <iterator>

namespace {

constexpr std::uint32_t RollSize = 100000;
constexpr std::uint32_t FirstModifierChance = 1000;
constexpr std::uint32_t LastModifierChance = 6000;
constexpr int AuraLevel = 5;
constexpr int MonsterHolyFreeze = 365;
constexpr std::uint8_t AuraEnchanted = 30;
constexpr std::size_t ModifierCount = 9;
constexpr std::uintptr_t ModifierTableRva = 0x2395FE0;

struct Affix {
    std::uint8_t id;
    const char* name;
    std::uintptr_t callback;
};

constexpr Affix Affixes[]{
    {6, "Extra Fast", 0x496300},
    {8, "Magic Resistant", 0x38E430},
    {27, "Spectral Hit", 0x38E430},
    {28, "Stone Skin", 0x38E430},
};

struct Aura {
    int skill;
    const char* name;
};

constexpr Aura Auras[]{
    {98, "Might"},
    {100, "Resist Fire"},
    {104, "Defiance"},
    {108, "Blessed Aim"},
    {115, "Vigor"},
    {MonsterHolyFreeze, "Holy Freeze"},
    {368, "Holy Fire"},
    {369, "Holy Shock"},
};

constexpr Aura AdditionalCheckpointAuras[]{
    {122, "Fanaticism"},
    {123, "Conviction"},
};

using ModifierSlotsFn = std::uint8_t*(__fastcall*)(void*) noexcept;
using ApplyAffixFn = void(__fastcall*)(void*) noexcept;
using SkillRecordFn = const void*(__fastcall*)(std::uint8_t bank, int skill) noexcept;
using GetRightSkillFn = const void*(__fastcall*)(void*) noexcept;
using SetSkillLevelFn = void(__fastcall*)(void*, int skill, int level, int assign) noexcept;
using SelectSkillFn = void(__fastcall*)(void*, int left, int skill, int owner) noexcept;
using FindSkillFn = const void*(__fastcall*)(void*, int skill, int owner) noexcept;

const D2RL::PluginContext* plugin = nullptr;
ModifierSlotsFn modifierSlots = nullptr;
ApplyAffixFn applyAffix = nullptr;
SkillRecordFn skillRecord = nullptr;
GetRightSkillFn getRightSkill = nullptr;
SetSkillLevelFn setSkillLevel = nullptr;
SelectSkillFn selectSkill = nullptr;
FindSkillFn findSkill = nullptr;

constexpr std::uint32_t ModifierChance(std::uint32_t floor) noexcept {
    return FirstModifierChance + (LastModifierChance - FirstModifierChance) * (floor - 1) / (DungeonFloorCount - 1);
}

constexpr std::uint8_t RollModifier(std::uint32_t floor, std::uint32_t seed) noexcept {
    const auto roll = MixDungeonSeed(seed ^ 0x5D82A4C1U);
    if (roll % RollSize >= ModifierChance(floor)) {
        return 0;
    }
    const auto category = MixDungeonSeed(roll ^ 0xC6BF21B9U) % 3;
    const auto choice = MixDungeonSeed(roll ^ 0xE49C7213U);
    return category == 0 ? static_cast<std::uint8_t>(std::size(Affixes) + 1 + choice % std::size(Auras))
                         : static_cast<std::uint8_t>(1 + choice % std::size(Affixes));
}

constexpr std::uint8_t ModifierSlotRead[]{
    0x48, 0x8B, 0x43, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x0A, 0x48, 0x83, 0xC0, 0x20,
};
constexpr std::uint8_t MonsterFlagRead[]{
    0x48, 0x8B, 0x47, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x2A, 0x0F, 0xB7, 0x48, 0x1A,
};
constexpr std::uint8_t ApplyAffixEntry[]{
    0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0xF1, 0xE8, 0xA1, 0xFA, 0xEE, 0xFF,
};
constexpr std::uint8_t AffixTableReference[]{0x4C, 0x8D, 0x35, 0x35, 0x77, 0xEF, 0x01};
constexpr std::uint8_t AffixDispatch[]{
    0x0F, 0xB6, 0x1C, 0x2F, 0x85, 0xDB, 0x74, 0x6E, 0x4D, 0x8B, 0x0C, 0xDE, 0x4D, 0x85, 0xC9,
    0x74, 0x0E, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x8B, 0xD3, 0x48, 0x8B, 0xCE, 0x41, 0xFF, 0xD1,
};
constexpr std::uint8_t ExtraFastEntry[]{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0xBD, 0x56, 0xEB, 0xFF,
};
constexpr std::uint8_t ResistanceEntry[]{
    0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x74, 0x0A,
};
constexpr std::uint8_t SkillRecordEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2,
};
constexpr std::uint8_t GetRightSkillRead[]{
    0x48, 0x8B, 0x8B, 0x00, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0x2E, 0x27, 0xFF, 0xFF,
};
constexpr std::uint8_t SetSkillLevelEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50,
    0x45, 0x8B, 0xE1, 0x4C, 0x63, 0xF2, 0x45, 0x8B, 0xF8, 0x48, 0x8B, 0xE9,
};
constexpr std::uint8_t SelectSkillEntry[]{
    0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02, 0x00, 0x00, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55,
};
constexpr std::uint8_t FindSkillEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xFA,
};
constexpr std::uint8_t BaseSkillLevelRead[]{0x8B, 0x72, 0x40, 0x45, 0x85, 0xC0};

bool ApplyAura(void* unit, const Aura& aura, bool checkpoint = false) noexcept {
    const auto* record = skillRecord(static_cast<std::uint8_t>(D2RL::DataTables::Bank::Rotw), aura.skill);
    if (record == nullptr || ReadField<std::int16_t>(record, 0) != aura.skill) {
        return false;
    }
    // Duriel only selects his innate Holy Freeze if his right skill is empty.
    // If he already selected it, replace it through the native skill selector.
    const auto* current = getRightSkill(unit);
    if (current != nullptr) {
        if (!checkpoint) return false;
        if (current == findSkill(unit, aura.skill, -1)) {
            if (ReadField<int>(current, 0x40) == AuraLevel) return true;
        } else if (current != findSkill(unit, MonsterHolyFreeze, -1)) {
            return false;
        }
    }
    // The game evaluates the selected skill's aura range at its assigned level.
    setSkillLevel(unit, aura.skill, AuraLevel, 1);
    const auto* skill = findSkill(unit, aura.skill, -1);
    if (skill == nullptr) {
        return false;
    }
    selectSkill(unit, 0, aura.skill, -1);
    return getRightSkill(unit) == skill;
}

} // namespace

bool EnableMonsterAuraSkills(const D2RL::PluginContext* context) noexcept {
    if (modifierSlots != nullptr) return true;
    if (!context->CheckExpectedBytes(0x38E34E, ModifierSlotRead, sizeof(ModifierSlotRead))
        || !context->CheckExpectedBytes(0x097790, SkillRecordEntry, sizeof(SkillRecordEntry))
        || !context->CheckExpectedBytes(0x34B421, GetRightSkillRead, sizeof(GetRightSkillRead))
        || !context->CheckExpectedBytes(0x433910, SetSkillLevelEntry, sizeof(SetSkillLevelEntry))
        || !context->CheckExpectedBytes(0x438A70, SelectSkillEntry, sizeof(SelectSkillEntry))
        || !context->CheckExpectedBytes(0x33DCD0, FindSkillEntry, sizeof(FindSkillEntry))
        || !context->CheckExpectedBytes(0x33D218, BaseSkillLevelRead, sizeof(BaseSkillLevelRead))) {
        return false;
    }
    modifierSlots = reinterpret_cast<ModifierSlotsFn>(context->exeBase + 0x38E310);
    skillRecord = reinterpret_cast<SkillRecordFn>(context->exeBase + 0x097790);
    getRightSkill = reinterpret_cast<GetRightSkillFn>(context->exeBase + 0x34B400);
    setSkillLevel = reinterpret_cast<SetSkillLevelFn>(context->exeBase + 0x433910);
    selectSkill = reinterpret_cast<SelectSkillFn>(context->exeBase + 0x438A70);
    findSkill = reinterpret_cast<FindSkillFn>(context->exeBase + 0x33DCD0);
    return true;
}

void EnablePackModifiers(const D2RL::PluginContext* context) noexcept {
    if (!EnableMonsterAuraSkills(context)
        || !context->CheckExpectedBytes(0x38E75E, MonsterFlagRead, sizeof(MonsterFlagRead))
        || !context->CheckExpectedBytes(0x49E860, ApplyAffixEntry, sizeof(ApplyAffixEntry))
        || !context->CheckExpectedBytes(0x49E8A4, AffixTableReference, sizeof(AffixTableReference))
        || !context->CheckExpectedBytes(0x49E8B7, AffixDispatch, sizeof(AffixDispatch))
        || !context->CheckExpectedBytes(0x496300, ExtraFastEntry, sizeof(ExtraFastEntry))
        || !context->CheckExpectedBytes(0x38E430, ResistanceEntry, sizeof(ResistanceEntry))) {
        context->LogWarn("Descent pack modifiers unavailable. Could not verify native affix and aura helpers.");
        return;
    }
    applyAffix = reinterpret_cast<ApplyAffixFn>(context->exeBase + 0x49E860);
    plugin = context;
    D2RL::LogInfoF(context, "Enabled Descent pack modifiers from %g%% to %g%%.",
                  FirstModifierChance * 100.0 / RollSize, LastModifierChance * 100.0 / RollSize);
}

void ApplyPackModifier(void* leader, std::uint32_t floor, int monsterId, std::uint32_t packSeed) noexcept {
    if (plugin == nullptr || leader == nullptr || floor == 0 || floor > DungeonFloorCount) {
        return;
    }
    const auto selection = RollModifier(floor, packSeed);
    if (selection == 0 || ReadField<std::uint32_t>(leader, 0) != 1 || ReadField<int>(leader, 4) != monsterId) {
        return;
    }
    const auto* data = ReadField<const void*>(leader, 0x10);
    if (data == nullptr || (ReadField<std::uint16_t>(data, 0x1A) & 0x1F) != 0) {
        return;
    }
    auto* slots = modifierSlots(leader);
    if (slots == nullptr) {
        return;
    }
    for (std::size_t index = 0; index < ModifierCount; ++index) {
        if (slots[index] != 0) {
            return;
        }
    }
    if (selection <= std::size(Affixes)) {
        const auto& affix = Affixes[selection - 1];
        const auto* table = reinterpret_cast<const std::uintptr_t*>(plugin->exeBase + ModifierTableRva);
        if (table[affix.id] == plugin->exeBase + affix.callback) {
            slots[0] = affix.id;
            applyAffix(leader);
        }
        return;
    }
    const auto& aura = Auras[selection - std::size(Affixes) - 1];
    if (ApplyAura(leader, aura)) {
        slots[0] = AuraEnchanted;
    }
}

void ApplyCheckpointAura(void* boss, std::uint32_t floor, std::uint32_t sessionSeed) noexcept {
    auto* slots = modifierSlots(boss);
    if (slots == nullptr) return;
    auto* end = slots + ModifierCount;
    auto* slot = std::find(slots, end, AuraEnchanted);
    if (slot == end) slot = std::find(slots, end, 0);
    if (slot == end) return;

    constexpr auto count = std::size(Auras) + std::size(AdditionalCheckpointAuras);
    const auto choice = MixDungeonSeed(sessionSeed ^ (floor * 0xB8026D41U)) % count;
    const auto& aura = choice < std::size(Auras) ? Auras[choice]
        : AdditionalCheckpointAuras[choice - std::size(Auras)];
    if (ApplyAura(boss, aura, true)) *slot = AuraEnchanted;
}
