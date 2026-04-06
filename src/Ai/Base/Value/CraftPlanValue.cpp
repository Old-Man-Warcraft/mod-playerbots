/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CraftPlanValue.h"

#include <iomanip>
#include <set>
#include <sstream>
#include <vector>

#include "BudgetValues.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"
#include "StatsWeightCalculator.h"

namespace
{
    constexpr float kOwnedItemScoreSlack = 0.98f;
    constexpr float kMajorUpgradeDeltaFloor = 12.0f;
    constexpr float kMajorUpgradeRelativeGain = 0.20f;
    constexpr uint32 kReservedUpgradePlanCount = 3;

    bool IsProfessionUtilityItem(uint32 itemId)
    {
        switch (itemId)
        {
            case 756:   // Tunnel Pick
            case 778:   // Kobold Excavation Pick
            case 1819:  // Gouging Pick
            case 1893:  // Miner's Revenge
            case 1959:  // Cold Iron Pick
            case 2901:  // Mining Pick
            case 4470:  // Simple Wood
            case 4471:  // Flint and Tinder
            case 5956:  // Blacksmith Hammer
            case 6218:  // Runed Copper Rod
            case 6219:  // Arclight Spanner
            case 6256:  // Fishing Pole
            case 6339:  // Runed Silver Rod
            case 7005:  // Skinning Knife
            case 9465:  // Digmaster 5000
            case 11130: // Runed Golden Rod
            case 11145: // Runed Truesilver Rod
            case 12709:
            case 16207: // Runed Arcanite Rod
            case 19901:
            case 20723: // Brann's Trusty Pick
            case 40772: // Gnomish Army Knife
            case 40892: // Hammer Pick
            case 40893: // Bladed Pickaxe
                return true;
            default:
                return false;
        }
    }

    bool IsFingerSlot(uint32 type)
    {
        return type == INVTYPE_FINGER;
    }

    bool IsTrinketSlot(uint32 type)
    {
        return type == INVTYPE_TRINKET;
    }

    bool IsWeaponHandType(uint32 type)
    {
        return type == INVTYPE_WEAPON || type == INVTYPE_2HWEAPON || type == INVTYPE_WEAPONMAINHAND ||
               type == INVTYPE_WEAPONOFFHAND || type == INVTYPE_HOLDABLE || type == INVTYPE_SHIELD;
    }
}

CraftPlanValue::~CraftPlanValue() = default;

std::string const CraftPlanValue::Format()
{
    CraftPlan plan = Calculate();
    if (plan.IsEmpty())
        return "<none>";

    std::ostringstream out;
    out << "spell=" << plan.spellId << ", item=" << plan.itemId << ", score=" << plan.score;
    if (plan.upgradeDelta > 0.0f)
        out << ", upgradeDelta=" << std::fixed << std::setprecision(1) << plan.upgradeDelta;
    if (!plan.reason.empty())
        out << ", reason=" << plan.reason;
    return out.str();
}

bool CraftPlanValue::IsSupportedCraftSpell(SpellInfo const* spellInfo) const
{
    return spellInfo && spellInfo->Effects[EFFECT_0].Effect == SPELL_EFFECT_CREATE_ITEM &&
           spellInfo->ReagentCount[EFFECT_0] > 0 && spellInfo->SchoolMask == 0;
}

uint32 CraftPlanValue::GetCreatedItemCount(SpellInfo const* spellInfo) const
{
    if (!spellInfo)
        return 1;

    int32 basePoints = spellInfo->Effects[EFFECT_0].BasePoints + 1;
    return std::max<uint32>(1, basePoints > 0 ? static_cast<uint32>(basePoints) : 1);
}

uint32 CraftPlanValue::GetCurrentCount(uint32 itemId) const
{
    return bot->GetItemCount(itemId, false);
}

bool CraftPlanValue::IsEquippableUsage(ItemUsage usage) const
{
    return usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_EQUIP;
}

bool CraftPlanValue::IsComparableGear(ItemTemplate const* leftProto, ItemTemplate const* rightProto) const
{
    if (!leftProto || !rightProto)
        return false;

    if (leftProto->Class != rightProto->Class)
        return false;

    if (leftProto->InventoryType == rightProto->InventoryType)
        return true;

    if (IsFingerSlot(leftProto->InventoryType) && IsFingerSlot(rightProto->InventoryType))
        return true;

    if (IsTrinketSlot(leftProto->InventoryType) && IsTrinketSlot(rightProto->InventoryType))
        return true;

    if (leftProto->Class == ITEM_CLASS_WEAPON && IsWeaponHandType(leftProto->InventoryType) &&
        IsWeaponHandType(rightProto->InventoryType))
        return true;

    return false;
}

bool CraftPlanValue::IsMajorUpgrade(CraftPlan const& plan) const
{
    if (plan.upgradeDelta <= 0.0f)
        return false;

    if (plan.equippedScore <= 0.0f)
        return plan.outputScore > 0.0f;

    return plan.upgradeDelta >= std::max(kMajorUpgradeDeltaFloor, plan.equippedScore * kMajorUpgradeRelativeGain);
}

bool CraftPlanValue::IsScarceReagent(ItemTemplate const* proto) const
{
    if (!proto)
        return false;

    if (proto->Bonding == BIND_WHEN_PICKED_UP)
        return true;

    if (proto->Quality >= ITEM_QUALITY_UNCOMMON)
        return true;

    if (!PlayerbotSpellRepository::Instance().IsItemBuyable(proto->ItemId))
        return true;

    return proto->BuyPrice >= GOLD || proto->SellPrice >= GOLD / 2;
}

bool CraftPlanValue::UsesReservedReagents(SpellInfo const* spellInfo, std::set<uint32> const& reservedReagents) const
{
    if (!spellInfo || reservedReagents.empty())
        return false;

    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->ReagentCount[i] <= 0 || !spellInfo->Reagent[i])
            continue;

        if (reservedReagents.find(spellInfo->Reagent[i]) != reservedReagents.end())
            return true;
    }

    return false;
}

void CraftPlanValue::EvaluateOwnedItemState(ItemTemplate const* proto, ItemUsage& usage, CraftPlan& plan) const
{
    if (!proto)
        return;

    if (!IsEquippableUsage(usage))
    {
        plan.isUsable = usage != ITEM_USAGE_NONE;
        return;
    }

    if (bot->BotCanUseItem(proto) != EQUIP_ERR_OK || proto->InventoryType == INVTYPE_NON_EQUIP)
    {
        plan.isUsable = false;
        usage = ITEM_USAGE_NONE;
        return;
    }

    plan.isUsable = true;

    StatsWeightCalculator calculator(bot);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);

    plan.outputScore = calculator.CalculateItem(proto->ItemId);

    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!equipped)
            continue;

        ItemTemplate const* ownedProto = equipped->GetTemplate();
        if (!IsComparableGear(proto, ownedProto))
            continue;

        float ownedScore = calculator.CalculateItem(ownedProto->ItemId,
            equipped->GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID));
        plan.equippedScore = std::max(plan.equippedScore, ownedScore);
        plan.bestOwnedScore = std::max(plan.bestOwnedScore, ownedScore);
    }

    auto scanOwnedItem = [&](Item* ownedItem)
    {
        if (!ownedItem)
            return;

        ItemTemplate const* ownedProto = ownedItem->GetTemplate();
        if (!IsComparableGear(proto, ownedProto))
            return;

        float ownedScore = calculator.CalculateItem(ownedProto->ItemId,
            ownedItem->GetInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID));
        plan.bestOwnedScore = std::max(plan.bestOwnedScore, ownedScore);
    };

    for (uint32 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        scanOwnedItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint32 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot));
        if (!bag)
            continue;

        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
            scanOwnedItem(bag->GetItemByPos(slot));
    }

    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
        scanOwnedItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint32 bagSlot = BANK_SLOT_BAG_START; bagSlot < BANK_SLOT_BAG_END; ++bagSlot)
    {
        Bag* bag = static_cast<Bag*>(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot));
        if (!bag)
            continue;

        for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
            scanOwnedItem(bag->GetItemByPos(slot));
    }

    plan.upgradeDelta = std::max(0.0f, plan.outputScore - plan.equippedScore);
    plan.ownsEqualOrBetter = plan.bestOwnedScore > 0.0f && plan.bestOwnedScore >= plan.outputScore * kOwnedItemScoreSlack;
    plan.isMajorUpgrade = IsMajorUpgrade(plan);

    if (plan.ownsEqualOrBetter)
        usage = ITEM_USAGE_NONE;
}

uint32 CraftPlanValue::GetDesiredCount(ItemTemplate const* proto, uint32 itemId, ItemUsage usage) const
{
    if (!proto)
        return 0;

    if (IsProfessionUtilityItem(itemId))
        return 1;

    if (usage == ITEM_USAGE_AMMO)
        return proto->GetMaxStackSize() * (bot->getClass() == CLASS_HUNTER ? 4 : 2);

    std::string const consumableType =
        ItemUsageValue::GetConsumableType(proto, bot->GetMaxPower(POWER_MANA) > 0);
    if (consumableType == "bandage")
        return proto->GetMaxStackSize() * 2;

    if (consumableType == "food" || consumableType == "drink" ||
        consumableType == "mana potion" || consumableType == "healing potion")
        return proto->GetMaxStackSize();

    return 0;
}

uint32 CraftPlanValue::GetReagentBuyCost(SpellInfo const* spellInfo) const
{
    if (!spellInfo)
        return 0;

    uint32 reagentCost = 0;
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->ReagentCount[i] <= 0 || !spellInfo->Reagent[i])
            continue;

        ItemTemplate const* reagentProto = sObjectMgr->GetItemTemplate(spellInfo->Reagent[i]);
        if (!reagentProto)
            continue;

        if (PlayerbotSpellRepository::Instance().IsItemBuyable(reagentProto->ItemId) && reagentProto->BuyPrice)
            reagentCost += spellInfo->ReagentCount[i] * reagentProto->BuyPrice;
    }

    return reagentCost;
}

uint32 CraftPlanValue::GetTotalReagentCount(SpellInfo const* spellInfo) const
{
    if (!spellInfo)
        return 0;

    uint32 totalCount = 0;
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
        if (spellInfo->ReagentCount[i] > 0 && spellInfo->Reagent[i])
            totalCount += spellInfo->ReagentCount[i];

    return totalCount;
}

uint32 CraftPlanValue::GetScarceReagentCount(SpellInfo const* spellInfo) const
{
    if (!spellInfo)
        return 0;

    uint32 scarceCount = 0;
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->ReagentCount[i] <= 0 || !spellInfo->Reagent[i])
            continue;

        ItemTemplate const* reagentProto = sObjectMgr->GetItemTemplate(spellInfo->Reagent[i]);
        if (!IsScarceReagent(reagentProto))
            continue;

        scarceCount += spellInfo->ReagentCount[i];
    }

    return scarceCount;
}

bool CraftPlanValue::HasVendorProfit(SpellInfo const* spellInfo, uint32 createdItemCount) const
{
    if (!spellInfo)
        return false;

    uint32 itemId = spellInfo->Effects[EFFECT_0].ItemType;
    ItemTemplate const* outputProto = sObjectMgr->GetItemTemplate(itemId);
    if (!outputProto || !outputProto->SellPrice)
        return false;

    uint32 reagentCost = GetReagentBuyCost(spellInfo);

    if (!reagentCost)
        return false;

    uint32 createdValue = createdItemCount * outputProto->SellPrice;
    return createdValue > (reagentCost * 12 / 10);
}

uint32 CraftPlanValue::ScoreSpell(SpellInfo const* spellInfo, ItemTemplate const* proto, ItemUsage usage,
                                  bool givesSkillUp, CraftPlan& plan) const
{
    if (!spellInfo || !proto)
        return 0;

    plan.reagentBuyCost = GetReagentBuyCost(spellInfo);
    plan.totalReagentCount = GetTotalReagentCount(spellInfo);
    plan.scarceReagentCount = GetScarceReagentCount(spellInfo);

    uint32 score = 0;
    switch (usage)
    {
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_EQUIP:
            score += 220;
            plan.purpose = CraftPlanPurpose::SelfUpgrade;
            if (plan.upgradeDelta > 0.0f)
            {
                score += static_cast<uint32>(std::min(120.0f, plan.upgradeDelta * 4.0f));
                if (plan.isMajorUpgrade)
                    score += 120;
            }
            if (plan.ownsEqualOrBetter)
                score = 0;
            break;
        case ITEM_USAGE_QUEST:
        case ITEM_USAGE_AMMO:
            score += 220;
            plan.purpose = CraftPlanPurpose::SelfSupply;
            break;
        case ITEM_USAGE_USE:
            score += 120;
            plan.purpose = CraftPlanPurpose::SelfSupply;
            break;
        case ITEM_USAGE_SKILL:
            score += 90;
            plan.purpose = CraftPlanPurpose::SelfSupply;
            break;
        default:
            break;
    }

    plan.currentCount = GetCurrentCount(plan.itemId);
    plan.desiredCount = GetDesiredCount(proto, plan.itemId, usage);

    if (plan.desiredCount)
    {
        if (plan.currentCount >= plan.desiredCount)
        {
            if (usage == ITEM_USAGE_USE || usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_AMMO)
                score = score > 50 ? score - 50 : 0;
        }
        else
        {
            uint32 deficit = plan.desiredCount - plan.currentCount;
            score += 40 + (80 * deficit / plan.desiredCount);
            if (plan.purpose == CraftPlanPurpose::None)
                plan.purpose = CraftPlanPurpose::SelfSupply;
        }
    }

    if (givesSkillUp)
    {
        score += 70;
        plan.givesSkillUp = true;

        SkillLineAbilityEntry const* skillLine = PlayerbotSpellRepository::Instance().GetSkillLine(spellInfo->Id);
        if (skillLine && skillLine->SkillLine)
        {
            plan.skillId = skillLine->SkillLine;
            uint32 skillValue = bot->GetSkillValue(skillLine->SkillLine);
            if (skillValue < skillLine->MinSkillLineRank)
                score += 20;
            else if (skillValue < (skillLine->TrivialSkillLineRankHigh + skillLine->MinSkillLineRank) / 2)
                score += 10;
        }

        if (plan.purpose == CraftPlanPurpose::None)
            plan.purpose = CraftPlanPurpose::SkillUp;

        if (plan.scarceReagentCount)
            score = score > (plan.scarceReagentCount * 15) ? score - (plan.scarceReagentCount * 15) : 0;

        if (plan.totalReagentCount > 1)
        {
            uint32 reagentPressure = (plan.totalReagentCount - 1) * 4;
            score = score > reagentPressure ? score - reagentPressure : 0;
        }
    }

    if (HasVendorProfit(spellInfo, GetCreatedItemCount(spellInfo)))
    {
        score += 45;
        plan.hasVendorProfit = true;
        if (plan.purpose == CraftPlanPurpose::None)
            plan.purpose = CraftPlanPurpose::VendorProfit;
    }

    uint8 bagPressure = AI_VALUE(uint8, "bag space");
    if (bagPressure > 80 && plan.desiredCount && plan.currentCount >= plan.desiredCount)
        score = score > 35 ? score - 35 : 0;

    if (AI_VALUE(bool, "should get money") && plan.hasVendorProfit)
        score += 15;

    if (AI_VALUE2(uint32, "free money for", static_cast<uint32>(NeedMoneyFor::tradeskill)) == 0 &&
        !plan.givesSkillUp && !plan.hasVendorProfit)
        score = score > 25 ? score - 25 : 0;

    if (!plan.isUsable && !givesSkillUp)
        score = 0;

    return score;
}

void CraftPlanValue::DescribePlan(CraftPlan& plan) const
{
    std::ostringstream out;

    switch (plan.purpose)
    {
        case CraftPlanPurpose::SelfUpgrade:
            out << (plan.isMajorUpgrade ? "major upgrade" : "upgrade");
            if (plan.upgradeDelta > 0.0f)
                out << " +" << std::fixed << std::setprecision(1) << plan.upgradeDelta;
            break;
        case CraftPlanPurpose::SelfSupply:
            out << "self supply";
            if (plan.desiredCount)
                out << " " << plan.currentCount << "/" << plan.desiredCount;
            break;
        case CraftPlanPurpose::SkillUp:
            out << "skill-up";
            break;
        case CraftPlanPurpose::VendorProfit:
            out << "vendor profit";
            break;
        default:
            out << "general use";
            break;
    }

    if (!plan.isUsable)
        out << "; unusable output";
    else if (plan.ownsEqualOrBetter)
        out << "; already owns equal-or-better item";

    if (plan.givesSkillUp)
        out << "; gives skill-up";

    if (plan.hasVendorProfit)
        out << "; vendor margin";

    if (plan.scarceReagentCount)
        out << "; scarce mats=" << plan.scarceReagentCount;

    if (plan.consumesReservedReagents)
        out << "; preserves mats for bigger upgrade";

    plan.reason = out.str();
}

CraftPlan CraftPlanValue::Calculate()
{
    std::vector<CraftPlan> candidates;

    for (PlayerSpellMap::const_iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        if (itr->second->State == PLAYERSPELL_REMOVED || !itr->second->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itr->first);
        if (!IsSupportedCraftSpell(spellInfo))
            continue;

        uint32 itemId = spellInfo->Effects[EFFECT_0].ItemType;
        if (!itemId)
            continue;

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (!botAI->CanCastSpell(itr->first, bot, true))
            continue;

        ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", std::to_string(itemId));
        bool givesSkillUp = ItemUsageValue::SpellGivesSkillUp(itr->first, bot);

        CraftPlan plan;
        plan.spellId = itr->first;
        plan.itemId = itemId;

        EvaluateOwnedItemState(proto, usage, plan);

        uint32 score = ScoreSpell(spellInfo, proto, usage, givesSkillUp, plan);
        if (!score)
            continue;

        plan.score = score;
        candidates.push_back(plan);
    }

    if (candidates.empty())
        return CraftPlan();

    std::vector<CraftPlan const*> upgradePlans;
    for (CraftPlan const& plan : candidates)
        if (plan.isMajorUpgrade)
            upgradePlans.push_back(&plan);

    std::sort(upgradePlans.begin(), upgradePlans.end(), [](CraftPlan const* left, CraftPlan const* right)
    {
        if (left->upgradeDelta != right->upgradeDelta)
            return left->upgradeDelta > right->upgradeDelta;

        if (left->score != right->score)
            return left->score > right->score;

        return left->spellId < right->spellId;
    });

    std::set<uint32> reservedReagents;
    for (uint32 index = 0; index < upgradePlans.size() && index < kReservedUpgradePlanCount; ++index)
    {
        SpellInfo const* reservedSpell = sSpellMgr->GetSpellInfo(upgradePlans[index]->spellId);
        if (!reservedSpell)
            continue;

        for (uint32 reagentIndex = 0; reagentIndex < MAX_SPELL_REAGENTS; ++reagentIndex)
        {
            if (reservedSpell->ReagentCount[reagentIndex] <= 0 || !reservedSpell->Reagent[reagentIndex])
                continue;

            ItemTemplate const* reagentProto = sObjectMgr->GetItemTemplate(reservedSpell->Reagent[reagentIndex]);
            if (!IsScarceReagent(reagentProto))
                continue;

            reservedReagents.insert(reservedSpell->Reagent[reagentIndex]);
        }
    }

    for (CraftPlan& plan : candidates)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(plan.spellId);
        if (!spellInfo)
            continue;

        if (!plan.isMajorUpgrade && UsesReservedReagents(spellInfo, reservedReagents))
        {
            plan.consumesReservedReagents = true;
            plan.score = plan.score > 120 ? plan.score - 120 : 0;
        }

        DescribePlan(plan);
    }

    std::sort(candidates.begin(), candidates.end(), [](CraftPlan const& left, CraftPlan const& right)
    {
        if (left.score != right.score)
            return left.score > right.score;

        if (left.isMajorUpgrade != right.isMajorUpgrade)
            return left.isMajorUpgrade;

        if (left.purpose != right.purpose)
            return static_cast<uint32>(left.purpose) < static_cast<uint32>(right.purpose);

        if (left.upgradeDelta != right.upgradeDelta)
            return left.upgradeDelta > right.upgradeDelta;

        if (left.scarceReagentCount != right.scarceReagentCount)
            return left.scarceReagentCount < right.scarceReagentCount;

        if (left.reagentBuyCost != right.reagentBuyCost)
            return left.reagentBuyCost < right.reagentBuyCost;

        if (left.totalReagentCount != right.totalReagentCount)
            return left.totalReagentCount < right.totalReagentCount;

        return left.spellId < right.spellId;
    });

    return candidates.front();
}
