/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "CraftPlanValue.h"

#include "BudgetValues.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"

namespace
{
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
}

CraftPlanValue::~CraftPlanValue() = default;

std::string const CraftPlanValue::Format()
{
    CraftPlan plan = Calculate();
    if (plan.IsEmpty())
        return "<none>";

    std::ostringstream out;
    out << "spell=" << plan.spellId << ", item=" << plan.itemId << ", score=" << plan.score;
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

bool CraftPlanValue::HasVendorProfit(SpellInfo const* spellInfo, uint32 createdItemCount) const
{
    if (!spellInfo)
        return false;

    uint32 itemId = spellInfo->Effects[EFFECT_0].ItemType;
    ItemTemplate const* outputProto = sObjectMgr->GetItemTemplate(itemId);
    if (!outputProto || !outputProto->SellPrice)
        return false;

    uint32 reagentCost = 0;
    for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->ReagentCount[i] <= 0 || !spellInfo->Reagent[i])
            continue;

        ItemTemplate const* reagentProto = sObjectMgr->GetItemTemplate(spellInfo->Reagent[i]);
        if (!reagentProto)
            return false;

        if (!PlayerbotSpellRepository::Instance().IsItemBuyable(reagentProto->ItemId) || !reagentProto->BuyPrice)
            return false;

        reagentCost += spellInfo->ReagentCount[i] * reagentProto->BuyPrice;
    }

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

    uint32 score = 0;
    switch (usage)
    {
        case ITEM_USAGE_REPLACE:
        case ITEM_USAGE_EQUIP:
        case ITEM_USAGE_QUEST:
        case ITEM_USAGE_AMMO:
            score += 220;
            plan.purpose = CraftPlanPurpose::SelfUpgrade;
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

    return score;
}

CraftPlan CraftPlanValue::Calculate()
{
    CraftPlan bestPlan;

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

        uint32 score = ScoreSpell(spellInfo, proto, usage, givesSkillUp, plan);
        if (!score)
            continue;

        plan.score = score;
        if (bestPlan.IsEmpty() || plan.score > bestPlan.score ||
            (plan.score == bestPlan.score && plan.givesSkillUp && !bestPlan.givesSkillUp) ||
            (plan.score == bestPlan.score && plan.hasVendorProfit && !bestPlan.hasVendorProfit))
            bestPlan = plan;
    }

    return bestPlan;
}
