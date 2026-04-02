/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_CRAFTPLANVALUE_H
#define _PLAYERBOT_CRAFTPLANVALUE_H

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
    bool givesSkillUp = false;
    bool hasVendorProfit = false;
    CraftPlanPurpose purpose = CraftPlanPurpose::None;

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
    uint32 GetDesiredCount(ItemTemplate const* proto, uint32 itemId, ItemUsage usage) const;
    uint32 GetCurrentCount(uint32 itemId) const;
    uint32 ScoreSpell(SpellInfo const* spellInfo, ItemTemplate const* proto, ItemUsage usage,
                      bool givesSkillUp, CraftPlan& plan) const;
    bool HasVendorProfit(SpellInfo const* spellInfo, uint32 createdItemCount) const;
    uint32 GetCreatedItemCount(SpellInfo const* spellInfo) const;
};

#endif
