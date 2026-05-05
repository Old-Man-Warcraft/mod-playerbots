/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

/// PlayerbotAPI.h — Public integration surface for external modules.
///
/// Other modules that wish to interact with mod-playerbots should include
/// ONLY this header. It intentionally limits what is exposed so that internal
/// implementation details can change without breaking consumers.
///
/// Quick-start
/// -----------
///
/// 1. Read a bot's current RPG status:
///
///   #include "PlayerbotAPI.h"
///
///   PlayerbotAI* ai = sPlayerbotsMgr.GetPlayerbotAI(player);
///   if (ai)
///   {
///       NewRpgStatus status = ai->rpgInfo.GetStatus();
///       // optionally inspect ai->rpgInfo.data via std::get_if<NewRpgInfo::DoQuest>() etc.
///   }
///
/// 2. Force a bot into a specific RPG state:
///
///   if (ai)
///       ai->rpgInfo.ChangeToDoQuest(questId, quest);
///
/// 3. React to RPG state changes (observer pattern):
///
///   class MyHook : public IPlayerbotRpgHook
///   {
///   public:
///       void OnRpgStatusChanged(Player* bot, NewRpgStatus oldStatus, NewRpgStatus newStatus) override
///       {
///           // react here
///       }
///   };
///
///   // At module startup (e.g. WorldScript::OnStartup):
///   static MyHook s_myHook;
///   sPlayerbotRpgHookMgr.RegisterHook(&s_myHook);
///
///   // At module shutdown (e.g. WorldScript::OnShutdown):
///   sPlayerbotRpgHookMgr.UnregisterHook(&s_myHook);
///
/// 4. Read structured performance monitor data:
///
///   PerfMonitorReport report = sPerfMonitor.BuildReport(false, false);
///   for (PerfMonitorReportRow const& row : report.rows)
///   {
///       // row.name, row.percentage, row.totalSeconds, row.averageMilliseconds
///   }
///
/// 5. Observe live performance samples:
///
///   class MyPerfHook : public IPlayerbotPerfHook
///   {
///   public:
///       void OnPerfMetricRecorded(PerfMetricRecord const& record) override
///       {
///           // inspect record.metric / record.name / record.elapsedUs
///       }
///   };
///
///   static MyPerfHook s_perfHook;
///   sPlayerbotPerfHookMgr.RegisterHook(&s_perfHook);

#ifndef _PLAYERBOT_API_H
#define _PLAYERBOT_API_H

// --- RPG state machine types -------------------------------------------------
#include "NewRpgInfo.h"          // NewRpgInfo, NewRpgStatistic, NewRpgStatus enum
#include "PlayerbotAIConfig.h"   // NewRpgStatus enum values (RPG_IDLE … RPG_STATUS_END)

// --- Hook / observer interface -----------------------------------------------
#include "PlayerbotRpgHook.h"    // IPlayerbotRpgHook, PlayerbotRpgHookMgr, sPlayerbotRpgHookMgr
#include "PlayerbotPerfHook.h"   // IPlayerbotPerfHook, PlayerbotPerfHookMgr, sPlayerbotPerfHookMgr

// --- Bot access --------------------------------------------------------------
#include "PlayerbotMgr.h"        // PlayerbotsMgr, sPlayerbotsMgr, GetPlayerbotAI()
#include "PlayerbotAI.h"         // PlayerbotAI — rpgInfo, rpgStatistic, lowPriorityQuest

// --- Performance monitor -----------------------------------------------------
#include "PerfMonitor.h"         // PerfMonitor, PerfMonitorReport, PerfMetricRecord, sPerfMonitor

#endif
