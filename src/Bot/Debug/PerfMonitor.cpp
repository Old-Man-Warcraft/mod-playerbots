/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PerfMonitor.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "Playerbots.h"
#include "PlayerbotPerfHook.h"

namespace
{
std::string GetMetricLabel(PerformanceMetric metric)
{
    switch (metric)
    {
        case PERF_MON_TRIGGER:
            return "Trigger";
        case PERF_MON_VALUE:
            return "Value";
        case PERF_MON_ACTION:
            return "Action";
        case PERF_MON_RNDBOT:
            return "RndBot";
        case PERF_MON_TOTAL:
            return "Total";
        default:
            return "?";
    }
}

std::string ToDisplayName(std::string const& name, bool fullStack)
{
    if (fullStack || name.find("|") == std::string::npos)
        return name;

    return name.substr(0, name.find("|")) + "]";
}

bool ShouldIncludeRow(PerfMonitorReportRow const& row, bool perTick)
{
    if (row.isSummary)
        return true;

    if (perTick)
        return row.percentage >= 0.1 || row.averageMilliseconds >= 0.25 ||
               row.stats.maxTime > 1000;

    return row.percentage >= 0.1 || row.averageMilliseconds >= 0.25 ||
           row.stats.maxTime > 1000;
}
}

PerfMonitorOperation* PerfMonitor::start(PerformanceMetric metric, std::string const name,
                                                       PerformanceStack* stack)
{
    if (!sPlayerbotAIConfig.perfMonEnabled)
        return nullptr;

    std::string stackName = name;

    if (stack)
    {
        if (!stack->empty())
        {
            std::ostringstream out;
            out << stackName << " [";

            for (std::vector<std::string>::reverse_iterator i = stack->rbegin(); i != stack->rend(); ++i)
                out << *i << (std::next(i) == stack->rend() ? "" : "|");

            out << "]";

            stackName = out.str().c_str();
        }

        stack->push_back(name);
    }

    std::lock_guard<std::mutex> guard(lock);
    PerformanceData* pd = data[metric][stackName];
    if (!pd)
    {
        pd = new PerformanceData();
        pd->minTime = 0;
        pd->maxTime = 0;
        pd->totalTime = 0;
        pd->count = 0;
        data[metric][stackName] = pd;
    }

    return new PerfMonitorOperation(pd, metric, name, stackName, stack);
}

PerfMonitorReport PerfMonitor::BuildReport(bool perTick, bool fullStack)
{
    PerfMonitorReport report;
    report.perTick = perTick;
    report.fullStack = fullStack;

    std::map<PerformanceMetric, std::map<std::string, PerfMonitorStatSnapshot>> snapshot;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto const& [metric, pdMap] : data)
        {
            for (auto const& [name, pd] : pdMap)
            {
                if (!pd)
                    continue;

                std::lock_guard<std::mutex> pdGuard(pd->lock);
                snapshot[metric][name] = PerfMonitorStatSnapshot{
                    pd->minTime,
                    pd->maxTime,
                    pd->totalTime,
                    pd->count,
                };
            }
        }
    }

    if (snapshot.empty())
        return report;

    uint64_t updateAITotalTime = 0;
    for (auto const& [name, stats] : snapshot[PERF_MON_TOTAL])
    {
        if (name.find("PlayerbotAI::UpdateAIInternal") != std::string::npos)
            updateAITotalTime += stats.totalTime;
    }

    auto fullTickItr = snapshot[PERF_MON_TOTAL].find("PlayerbotAIBase::FullTick");
    uint64_t fullTickTotalTime = fullTickItr != snapshot[PERF_MON_TOTAL].end()
        ? fullTickItr->second.totalTime : 0;
    double fullTickCount = fullTickItr != snapshot[PERF_MON_TOTAL].end()
        ? fullTickItr->second.count : 0.0;

    report.referenceTotalTime = perTick ? fullTickTotalTime : updateAITotalTime;
    report.referenceCount = perTick ? fullTickCount : 0.0;

    for (auto const& [metric, pdMap] : snapshot)
    {
        std::vector<std::pair<std::string, PerfMonitorStatSnapshot>> entries;
        for (auto const& [name, stats] : pdMap)
        {
            if (!perTick && metric == PERF_MON_TOTAL &&
                name.find("PlayerbotAI::UpdateAIInternal") == std::string::npos)
                continue;

            entries.emplace_back(name, stats);
        }

        std::sort(entries.begin(), entries.end(),
                  [](auto const& left, auto const& right)
                  {
                      return left.second.totalTime < right.second.totalTime;
                  });

        uint64_t typeTotalTime = 0;
        uint64_t typeMinTime = 0xffffffffu;
        uint64_t typeMaxTime = 0;
        uint32_t typeCount = 0;
        std::string const metricLabel = GetMetricLabel(metric);

        for (auto const& [name, stats] : entries)
        {
            typeTotalTime += stats.totalTime;
            typeCount += stats.count;
            if (typeMinTime > stats.minTime)
                typeMinTime = stats.minTime;
            if (typeMaxTime < stats.maxTime)
                typeMaxTime = stats.maxTime;

            PerfMonitorReportRow row;
            row.metric = metric;
            row.metricLabel = metricLabel;
            row.name = ToDisplayName(name, fullStack);
            row.stats = stats;
            row.percentage = report.referenceTotalTime
                ? static_cast<double>(stats.totalTime) /
                      static_cast<double>(report.referenceTotalTime) * 100.0
                : 0.0;
            row.totalSeconds = static_cast<double>(stats.totalTime) / 1000000.0;
            row.perTickMilliseconds = fullTickCount
                ? static_cast<double>(stats.totalTime) / fullTickCount / 1000.0
                : 0.0;
            row.minMilliseconds = static_cast<double>(stats.minTime) / 1000.0;
            row.maxMilliseconds = static_cast<double>(stats.maxTime) / 1000.0;
            row.averageMilliseconds = stats.count
                ? static_cast<double>(stats.totalTime) /
                      static_cast<double>(stats.count) / 1000.0
                : 0.0;
            row.amount = perTick && fullTickCount
                ? static_cast<double>(stats.count) / fullTickCount
                : static_cast<double>(stats.count);

            if (ShouldIncludeRow(row, perTick))
                report.rows.push_back(row);
        }

        if (!entries.empty() && (!perTick || metric != PERF_MON_TOTAL))
        {
            PerfMonitorReportRow summary;
            summary.metric = metric;
            summary.metricLabel = metricLabel;
            summary.name = "Total";
            summary.isSummary = true;
            summary.stats = PerfMonitorStatSnapshot{
                typeMinTime == 0xffffffffu ? 0 : typeMinTime,
                typeMaxTime,
                typeTotalTime,
                typeCount,
            };
            summary.percentage = report.referenceTotalTime
                ? static_cast<double>(typeTotalTime) /
                      static_cast<double>(report.referenceTotalTime) * 100.0
                : 0.0;
            summary.totalSeconds =
                static_cast<double>(typeTotalTime) / 1000000.0;
            summary.perTickMilliseconds = fullTickCount
                ? static_cast<double>(typeTotalTime) / fullTickCount / 1000.0
                : 0.0;
            summary.minMilliseconds =
                static_cast<double>(summary.stats.minTime) / 1000.0;
            summary.maxMilliseconds =
                static_cast<double>(summary.stats.maxTime) / 1000.0;
            summary.averageMilliseconds = typeCount
                ? static_cast<double>(typeTotalTime) /
                      static_cast<double>(typeCount) / 1000.0
                : 0.0;
            summary.amount = perTick && fullTickCount
                ? static_cast<double>(typeCount) / fullTickCount
                : static_cast<double>(typeCount);
            report.rows.push_back(summary);
        }
    }

    if (sPlayerbotAIConfig.perfMonEnabled && sPlayerbotAIConfig.enablePerfHooks)
        sPlayerbotPerfHookMgr.NotifyReportGenerated(report);
    return report;
}

void PerfMonitor::PrintStats(bool perTick, bool fullStack)
{
    PerfMonitorReport report = BuildReport(perTick, fullStack);
    if (report.rows.empty())
        return;

    if (!perTick)
    {
        LOG_INFO(
            "playerbots",
            "--------------------------------------[TOTAL BOT]------------------------------------------------------");
        LOG_INFO("playerbots",
                 "percentage     time  |     min ..     max (      avg  of      count) - type      : name");
        LOG_INFO(
            "playerbots",
            "-------------------------------------------------------------------------------------------------------");

        PerformanceMetric currentMetric = static_cast<PerformanceMetric>(-1);
        for (PerfMonitorReportRow const& row : report.rows)
        {
            if (currentMetric != row.metric)
            {
                if (currentMetric != static_cast<PerformanceMetric>(-1))
                    LOG_INFO("playerbots", " ");
                currentMetric = row.metric;
            }

            LOG_INFO("playerbots",
                     "{:7.3f}% {:10.3f}s | {:7.1f} .. {:7.1f} ({:10.3f} of {:10d}) - {:6}    : {}",
                     row.percentage, row.totalSeconds, row.minMilliseconds,
                     row.maxMilliseconds, row.averageMilliseconds,
                     row.stats.count, row.metricLabel.c_str(), row.name.c_str());
        }

        LOG_INFO("playerbots", " ");
    }
    else
    {
        LOG_INFO(
            "playerbots",
            "---------------------------------------[PER TICK]------------------------------------------------------");
        LOG_INFO("playerbots",
                 "percentage     time  |     min ..     max (      avg  of      count) - type      : name");
        LOG_INFO(
            "playerbots",
            "-------------------------------------------------------------------------------------------------------");

        PerformanceMetric currentMetric = static_cast<PerformanceMetric>(-1);
        for (PerfMonitorReportRow const& row : report.rows)
        {
            if (currentMetric != row.metric)
            {
                if (currentMetric != static_cast<PerformanceMetric>(-1))
                    LOG_INFO("playerbots", " ");
                currentMetric = row.metric;
            }

            LOG_INFO("playerbots",
                     "{:7.3f}% {:9.3f}ms | {:7.1f} .. {:7.1f} ({:10.3f} of {:10.2f}) - {:6}    : {}",
                     row.percentage, row.perTickMilliseconds,
                     row.minMilliseconds, row.maxMilliseconds,
                     row.averageMilliseconds, row.amount,
                     row.metricLabel.c_str(), row.name.c_str());
        }

        LOG_INFO("playerbots", " ");
    }
}

void PerfMonitor::Reset()
{
    for (std::map<PerformanceMetric, std::map<std::string, PerformanceData*>>::iterator i = data.begin();
         i != data.end(); ++i)
    {
        std::map<std::string, PerformanceData*> pdMap = i->second;
        for (std::map<std::string, PerformanceData*>::iterator j = pdMap.begin(); j != pdMap.end(); ++j)
        {
            PerformanceData* pd = j->second;
            std::lock_guard<std::mutex> guard(pd->lock);
            pd->minTime = 0;
            pd->maxTime = 0;
            pd->totalTime = 0;
            pd->count = 0;
        }
    }
}

PerfMonitorOperation::PerfMonitorOperation(PerformanceData* data,
                                                                                     PerformanceMetric metric,
                                                                                     std::string const name,
                                                                                     std::string const stackName,
                                                                                     PerformanceStack* stack)
        : data(data), metric(metric), name(name), stackName(stackName),
            stack(stack)
{
    started = (std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()))
                  .time_since_epoch();
}

void PerfMonitorOperation::finish()
{
    std::chrono::microseconds finished =
        (std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()))
            .time_since_epoch();
    uint64 elapsed = (finished - started).count();

    PerfMetricRecord record;
    record.metric = metric;
    record.name = name;
    record.stackName = stackName;
    record.elapsedUs = elapsed;

    {
        std::lock_guard<std::mutex> guard(data->lock);
        if (elapsed > 0)
        {
            if (!data->minTime || data->minTime > elapsed)
                data->minTime = elapsed;

            if (!data->maxTime || data->maxTime < elapsed)
                data->maxTime = elapsed;

            data->totalTime += elapsed;
        }

        ++data->count;
        record.aggregate = PerfMonitorStatSnapshot{
            data->minTime,
            data->maxTime,
            data->totalTime,
            data->count,
        };
    }

    if (stack)
    {
        stack->erase(std::remove(stack->begin(), stack->end(), name), stack->end());
    }

    if (sPlayerbotAIConfig.perfMonEnabled && sPlayerbotAIConfig.enablePerfHooks)
        sPlayerbotPerfHookMgr.NotifyMetricRecorded(record);

    delete this;
}
