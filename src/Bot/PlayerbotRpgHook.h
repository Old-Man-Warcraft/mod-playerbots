/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PLAYERBOTrpghook_H
#define _PLAYERBOT_PLAYERBOTrpghook_H

#include <algorithm>
#include <shared_mutex>
#include <string>
#include <vector>

#include "PlayerbotAIConfig.h"

class Player;
struct NewRpgInfo;

/// Describes a custom RPG status candidate contributed by an external hook.
/// @param tag     Opaque string identifier chosen by the hook (passed back via ExecuteCustomStatus).
/// @param weight  Relative probability weight, same units as RpgStatusProbWeight config values.
struct RpgStatusCandidate
{
    std::string tag;
    uint32 weight{0};
};

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

    /// Called during RandomChangeStatus to let external modules inject additional status
    /// candidates into the weighted lottery. Return an empty vector (default) to add nothing.
    /// Each candidate has a string tag and a weight. If the hook's candidate wins the
    /// lottery, ExecuteCustomStatus is called with that tag.
    /// @param bot  The bot for which status candidates are being collected.
    virtual std::vector<RpgStatusCandidate> GetExtraStatusCandidates(Player* /*bot*/) { return {}; }

    /// Called when a candidate returned by GetExtraStatusCandidates wins the lottery.
    /// The hook should apply the appropriate state change to info and return true on
    /// success, false if the status cannot be executed (lottery will fall back to rest).
    /// @param bot  The bot whose status should change.
    /// @param info Reference to the bot's live NewRpgInfo.
    /// @param tag  The tag from the winning RpgStatusCandidate.
    virtual bool ExecuteCustomStatus(Player* /*bot*/, NewRpgInfo& /*info*/, std::string const& /*tag*/) { return false; }
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
        {
            std::unique_lock<std::shared_mutex> guard(_hooksLock);
            _hooks.push_back(hook);
        }
    }

    void UnregisterHook(IPlayerbotRpgHook* hook)
    {
        std::unique_lock<std::shared_mutex> guard(_hooksLock);
        _hooks.erase(std::remove(_hooks.begin(), _hooks.end(), hook), _hooks.end());
    }

    /// Notify all hooks that a status change occurred.
    void NotifyStatusChanged(Player* bot, NewRpgStatus oldStatus, NewRpgStatus newStatus)
    {
        if (!sPlayerbotAIConfig.enableRpgHooks)
            return;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        for (IPlayerbotRpgHook* hook : _hooks)
            hook->OnRpgStatusChanged(bot, oldStatus, newStatus);
    }

    /// Give hooks a chance to suppress the built-in status update.
    /// Returns true if any hook consumed the update.
    bool NotifyStatusUpdate(Player* bot, NewRpgInfo& info)
    {
        if (!sPlayerbotAIConfig.enableRpgHooks)
            return false;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        for (IPlayerbotRpgHook* hook : _hooks)
        {
            if (hook->OnRpgStatusUpdate(bot, info))
                return true;
        }
        return false;
    }

    /// Collect extra status candidates from all hooks.
    /// Each element is (hook*, candidate) so the winning hook can be dispatched.
    std::vector<std::pair<IPlayerbotRpgHook*, RpgStatusCandidate>> CollectExtraCandidates(Player* bot)
    {
        std::vector<std::pair<IPlayerbotRpgHook*, RpgStatusCandidate>> result;

        if (!sPlayerbotAIConfig.enableRpgHooks)
            return result;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        for (IPlayerbotRpgHook* hook : _hooks)
        {
            for (RpgStatusCandidate& c : hook->GetExtraStatusCandidates(bot))
                result.emplace_back(hook, std::move(c));
        }
        return result;
    }

    /// Dispatch ExecuteCustomStatus to the hook that owns the winning candidate.
    bool DispatchCustomStatus(Player* bot, NewRpgInfo& info, IPlayerbotRpgHook* owner, std::string const& tag)
    {
        if (!sPlayerbotAIConfig.enableRpgHooks || !owner)
            return false;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        return owner->ExecuteCustomStatus(bot, info, tag);
    }

    std::vector<IPlayerbotRpgHook*> GetHooks() const
    {
        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        return _hooks;
    }

private:
    PlayerbotRpgHookMgr() = default;
    ~PlayerbotRpgHookMgr() = default;
    PlayerbotRpgHookMgr(PlayerbotRpgHookMgr const&) = delete;
    PlayerbotRpgHookMgr& operator=(PlayerbotRpgHookMgr const&) = delete;

    mutable std::shared_mutex _hooksLock;
    std::vector<IPlayerbotRpgHook*> _hooks;
};

#define sPlayerbotRpgHookMgr PlayerbotRpgHookMgr::instance()

#endif
