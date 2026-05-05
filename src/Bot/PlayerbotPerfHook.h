/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PLAYERBOTPERFHOOK_H
#define _PLAYERBOT_PLAYERBOTPERFHOOK_H

#include <algorithm>
#include <shared_mutex>
#include <vector>

#include "PerfMonitor.h"

/// Observer interface for performance monitor events and reports.
///
/// Register implementations during module startup to receive raw timing samples
/// and structured reports equivalent to the data behind `.playerbots pmon`.
class IPlayerbotPerfHook
{
public:
    virtual ~IPlayerbotPerfHook() = default;

    /// Called whenever a monitored performance operation finishes.
    virtual void OnPerfMetricRecorded(PerfMetricRecord const& /*record*/) {}

    /// Called whenever a structured report is generated from the monitor.
    virtual void OnPerfReportGenerated(PerfMonitorReport const& /*report*/) {}
};

class PlayerbotPerfHookMgr
{
public:
    static PlayerbotPerfHookMgr& instance()
    {
        static PlayerbotPerfHookMgr inst;
        return inst;
    }

    void RegisterHook(IPlayerbotPerfHook* hook)
    {
        if (hook)
        {
            std::unique_lock<std::shared_mutex> guard(_hooksLock);
            _hooks.push_back(hook);
        }
    }

    void UnregisterHook(IPlayerbotPerfHook* hook)
    {
        std::unique_lock<std::shared_mutex> guard(_hooksLock);
        _hooks.erase(std::remove(_hooks.begin(), _hooks.end(), hook), _hooks.end());
    }

    void NotifyMetricRecorded(PerfMetricRecord const& record)
    {
        if (!sPlayerbotAIConfig.enablePerfHooks)
            return;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        for (IPlayerbotPerfHook* hook : _hooks)
            hook->OnPerfMetricRecorded(record);
    }

    void NotifyReportGenerated(PerfMonitorReport const& report)
    {
        if (!sPlayerbotAIConfig.enablePerfHooks)
            return;

        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        for (IPlayerbotPerfHook* hook : _hooks)
            hook->OnPerfReportGenerated(report);
    }

    std::vector<IPlayerbotPerfHook*> GetHooks() const
    {
        std::shared_lock<std::shared_mutex> guard(_hooksLock);
        return _hooks;
    }

private:
    PlayerbotPerfHookMgr() = default;
    ~PlayerbotPerfHookMgr() = default;
    PlayerbotPerfHookMgr(PlayerbotPerfHookMgr const&) = delete;
    PlayerbotPerfHookMgr& operator=(PlayerbotPerfHookMgr const&) = delete;

    mutable std::shared_mutex _hooksLock;
    std::vector<IPlayerbotPerfHook*> _hooks;
};

#define sPlayerbotPerfHookMgr PlayerbotPerfHookMgr::instance()

#endif
