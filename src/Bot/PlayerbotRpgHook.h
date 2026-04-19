/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PLAYERBOTrpghook_H
#define _PLAYERBOT_PLAYERBOTrpghook_H

#include <vector>

#include "PlayerbotAIConfig.h"

class Player;
struct NewRpgInfo;

/// Interface for external modules to observe and interact with the New RPG state machine.
///
/// Register a concrete implementation via PlayerbotRpgHookMgr::RegisterHook() during
/// module initialisation (e.g. from your ScriptLoader or WorldScript::OnStartup).
/// Hooks are called synchronously on the bot's update thread — keep implementations fast.
class IPlayerbotRpgHook
{
public:
    virtual ~IPlayerbotRpgHook() = default;

    /// Called immediately after a bot's RPG status has changed.
    /// @param bot        The bot player whose status changed.
    /// @param oldStatus  The status the bot was in before the transition.
    /// @param newStatus  The status the bot has transitioned into.
    virtual void OnRpgStatusChanged(Player* /*bot*/, NewRpgStatus /*oldStatus*/, NewRpgStatus /*newStatus*/) {}

    /// Called at the start of NewRpgStatusUpdateAction::Execute, before any built-in
    /// transition logic runs. Return true to suppress the built-in update entirely for
    /// this tick. Returning false lets normal processing continue.
    /// Only the first hook that returns true takes effect.
    /// @param bot   The bot being updated.
    /// @param info  Reference to the bot's live NewRpgInfo (may be mutated by the hook).
    virtual bool OnRpgStatusUpdate(Player* /*bot*/, NewRpgInfo& /*info*/) { return false; }
};

/// Singleton registry for IPlayerbotRpgHook implementations.
/// External modules call RegisterHook() once; the registry does NOT take ownership of the
/// pointer — the caller is responsible for keeping it alive for the server lifetime.
class PlayerbotRpgHookMgr
{
public:
    static PlayerbotRpgHookMgr& instance()
    {
        static PlayerbotRpgHookMgr inst;
        return inst;
    }

    void RegisterHook(IPlayerbotRpgHook* hook)
    {
        if (hook)
            _hooks.push_back(hook);
    }

    void UnregisterHook(IPlayerbotRpgHook* hook)
    {
        _hooks.erase(std::remove(_hooks.begin(), _hooks.end(), hook), _hooks.end());
    }

    /// Notify all hooks that a status change occurred.
    void NotifyStatusChanged(Player* bot, NewRpgStatus oldStatus, NewRpgStatus newStatus)
    {
        for (IPlayerbotRpgHook* hook : _hooks)
            hook->OnRpgStatusChanged(bot, oldStatus, newStatus);
    }

    /// Give hooks a chance to suppress the built-in status update.
    /// Returns true if any hook consumed the update.
    bool NotifyStatusUpdate(Player* bot, NewRpgInfo& info)
    {
        for (IPlayerbotRpgHook* hook : _hooks)
        {
            if (hook->OnRpgStatusUpdate(bot, info))
                return true;
        }
        return false;
    }

    std::vector<IPlayerbotRpgHook*> const& GetHooks() const { return _hooks; }

private:
    PlayerbotRpgHookMgr() = default;
    ~PlayerbotRpgHookMgr() = default;
    PlayerbotRpgHookMgr(PlayerbotRpgHookMgr const&) = delete;
    PlayerbotRpgHookMgr& operator=(PlayerbotRpgHookMgr const&) = delete;

    std::vector<IPlayerbotRpgHook*> _hooks;
};

#define sPlayerbotRpgHookMgr PlayerbotRpgHookMgr::instance()

#endif
