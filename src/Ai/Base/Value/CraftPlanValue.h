/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_CRAFTPLANVALUE_H
#define _PLAYERBOT_CRAFTPLANVALUE_H

#include <set>
#include <string>

#include "ItemUsageValue.h"
#include "NamedObjectContext.h"
#include "Value.h"

class PlayerbotAI;
struct SpellInfo;
struct ItemTemplate;

enum class CraftPlanPurpose : uint8
{
    None = 0,
    SelfUpgrade = 1,
    SelfSupply = 2,
    SkillUp = 3,
    VendorProfit = 4
};

struct CraftPlan
{
    uint32 spellId = 0;
    uint32 itemId = 0;
    uint32 skillId = 0;
    uint32 score = 0;
    uint32 currentCount = 0;
    uint32 desiredCount = 0;
    uint32 reagentBuyCost = 0;
    uint32 totalReagentCount = 0;
    uint32 scarceReagentCount = 0;
    bool givesSkillUp = false;
    bool hasVendorProfit = false;
    bool isUsable = false;
    bool ownsEqualOrBetter = false;
    bool isMajorUpgrade = false;
    bool consumesReservedReagents = false;
    CraftPlanPurpose purpose = CraftPlanPurpose::None;
    float outputScore = 0.0f;
    float equippedScore = 0.0f;
    float bestOwnedScore = 0.0f;
    float upgradeDelta = 0.0f;
    std::string reason;

    bool IsEmpty() const { return spellId == 0 || itemId == 0; }
};

class CraftPlanValue : public CalculatedValue<CraftPlan>
{
public:
    CraftPlanValue(PlayerbotAI* botAI) : CalculatedValue<CraftPlan>(botAI, "craft plan", 2 * 1000) {}
    ~CraftPlanValue() override;

    CraftPlan Calculate() override;
    std::string const Format() override;

private:
    bool IsSupportedCraftSpell(SpellInfo const* spellInfo) const;
    bool IsEquippableUsage(ItemUsage usage) const;
    bool IsComparableGear(ItemTemplate const* leftProto, ItemTemplate const* rightProto) const;
    bool IsMajorUpgrade(CraftPlan const& plan) const;
    bool IsScarceReagent(ItemTemplate const* proto) const;
    bool UsesReservedReagents(SpellInfo const* spellInfo, std::set<uint32> const& reservedReagents) const;
    void EvaluateOwnedItemState(ItemTemplate const* proto, ItemUsage& usage, CraftPlan& plan) const;
    void DescribePlan(CraftPlan& plan) const;
    uint32 GetDesiredCount(ItemTemplate const* proto, uint32 itemId, ItemUsage usage) const;
    uint32 GetCurrentCount(uint32 itemId) const;
    uint32 GetReagentBuyCost(SpellInfo const* spellInfo) const;
    uint32 GetTotalReagentCount(SpellInfo const* spellInfo) const;
    uint32 GetScarceReagentCount(SpellInfo const* spellInfo) const;
    uint32 ScoreSpell(SpellInfo const* spellInfo, ItemTemplate const* proto, ItemUsage usage,
                      bool givesSkillUp, CraftPlan& plan) const;
    bool HasVendorProfit(SpellInfo const* spellInfo, uint32 createdItemCount) const;
    uint32 GetCreatedItemCount(SpellInfo const* spellInfo) const;
};

#endif
