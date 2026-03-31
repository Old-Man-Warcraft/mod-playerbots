/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "PerfMonitor.h"

#include "Playerbots.h"

namespace
{
    std::string GetMetricName(PerformanceMetric metric)
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

    return new PerfMonitorOperation(pd, name, stack);
}

void PerfMonitor::PrintStats(bool perTick, bool fullStack)
{
    PrintStatsInternal(
        [](std::string const& line)
        {
            LOG_INFO("playerbots", "{}", line);
        },
        perTick, fullStack);
}

bool PerfMonitor::PrintStatsToFile(std::string const& fileName, bool perTick,
                                   bool fullStack)
{
    if (!sPlayerbotAIConfig.openLog(fileName, "a"))
        return false;

    std::string const reportType = perTick ? "tick" : (fullStack ? "stack" : "total");
    std::string const timestamp = sPlayerbotAIConfig.GetTimestampStr();

    sPlayerbotAIConfig.log(fileName, "");
    sPlayerbotAIConfig.log(fileName,
                           "==================== [%s] playerbots pmon %s ====================",
                           timestamp.c_str(), reportType.c_str());

    if (data.empty())
    {
        sPlayerbotAIConfig.log(fileName, "No performance data collected.");
        return true;
    }

    PrintStatsInternal(
        [fileName](std::string const& line)
        {
            sPlayerbotAIConfig.log(fileName, "%s", line.c_str());
        },
        perTick, fullStack);

    return true;
}

void PerfMonitor::PrintStatsInternal(
    std::function<void(std::string const&)> const& sink, bool perTick,
    bool fullStack)
{
    if (data.empty())
        return;

    if (!perTick)
    {
        float updateAITotalTime = 0;
        for (auto& map : data[PERF_MON_TOTAL])
            if (map.first.find("PlayerbotAI::UpdateAIInternal") != std::string::npos)
                updateAITotalTime += map.second->totalTime;

        sink("--------------------------------------[TOTAL BOT]------------------------------------------------------");
        sink("percentage     time  |     min ..     max (      avg  of      count) - type      : name");
        sink("-------------------------------------------------------------------------------------------------------");

        for (std::map<PerformanceMetric, std::map<std::string, PerformanceData*>>::iterator i = data.begin();
             i != data.end(); ++i)
        {
            std::map<std::string, PerformanceData*> pdMap = i->second;

            std::string const key = GetMetricName(i->first);

            std::vector<std::string> names;

            for (std::map<std::string, PerformanceData*>::iterator j = pdMap.begin(); j != pdMap.end(); ++j)
            {
                if (key == "Total" && j->first.find("PlayerbotAI::UpdateAIInternal") == std::string::npos)
                    continue;

                names.push_back(j->first);
            }

            std::sort(names.begin(), names.end(),
                      [pdMap](std::string const i, std::string const j)
                      { return pdMap.at(i)->totalTime < pdMap.at(j)->totalTime; });

            uint64 typeTotalTime = 0;
            uint64 typeMinTime = 0xffffffffu;
            uint64 typeMaxTime = 0;
            uint32 typeCount = 0;
            for (auto& name : names)
            {
                PerformanceData* pd = pdMap[name];
                typeTotalTime += pd->totalTime;
                typeCount += pd->count;
                if (typeMinTime > pd->minTime)
                    typeMinTime = pd->minTime;
                if (typeMaxTime < pd->maxTime)
                    typeMaxTime = pd->maxTime;
                float perc = (float)pd->totalTime / updateAITotalTime * 100.0f;
                float time = (float)pd->totalTime / 1000000.0f;
                float minTime = (float)pd->minTime / 1000.0f;
                float maxTime = (float)pd->maxTime / 1000.0f;
                float avg = (float)pd->totalTime / (float)pd->count / 1000.0f;
                std::string disName = name;
                if (!fullStack && disName.find("|") != std::string::npos)
                    disName = disName.substr(0, disName.find("|")) + "]";

                if (perc >= 0.1f || avg >= 0.25f || pd->maxTime > 1000)
                {
                    sink(Acore::StringFormat(
                        "{:7.3f}% {:10.3f}s | {:7.1f} .. {:7.1f} ({:10.3f} of {:10d}) - {:6}    : {}",
                        perc, time, minTime, maxTime, avg, pd->count, key.c_str(),
                        disName.c_str()));
                }
            }
            float tPerc = (float)typeTotalTime / (float)updateAITotalTime * 100.0f;
            float tTime = (float)typeTotalTime / 1000000.0f;
            float tMinTime = (float)typeMinTime / 1000.0f;
            float tMaxTime = (float)typeMaxTime / 1000.0f;
            float tAvg = (float)typeTotalTime / (float)typeCount / 1000.0f;
            sink(Acore::StringFormat(
                "{:7.3f}% {:10.3f}s | {:7.1f} .. {:7.1f} ({:10.3f} of {:10d}) - {:6}    : {}",
                tPerc, tTime, tMinTime, tMaxTime, tAvg, typeCount, key.c_str(),
                "Total"));
            sink(" ");
        }
    }
    else
    {
        float fullTickCount = data[PERF_MON_TOTAL]["PlayerbotAIBase::FullTick"]->count;
        float fullTickTotalTime = data[PERF_MON_TOTAL]["PlayerbotAIBase::FullTick"]->totalTime;

        sink("---------------------------------------[PER TICK]------------------------------------------------------");
        sink("percentage     time  |     min ..     max (      avg  of      count) - type      : name");
        sink("-------------------------------------------------------------------------------------------------------");

        for (std::map<PerformanceMetric, std::map<std::string, PerformanceData*>>::iterator i = data.begin();
             i != data.end(); ++i)
        {
            std::map<std::string, PerformanceData*> pdMap = i->second;

            std::string const key = GetMetricName(i->first);

            std::vector<std::string> names;

            for (std::map<std::string, PerformanceData*>::iterator j = pdMap.begin(); j != pdMap.end(); ++j)
            {
                names.push_back(j->first);
            }

            std::sort(names.begin(), names.end(),
                      [pdMap](std::string const i, std::string const j)
                      { return pdMap.at(i)->totalTime < pdMap.at(j)->totalTime; });

            uint64 typeTotalTime = 0;
            uint64 typeMinTime = 0xffffffffu;
            uint64 typeMaxTime = 0;
            uint32 typeCount = 0;
            for (auto& name : names)
            {
                PerformanceData* pd = pdMap[name];
                typeTotalTime += pd->totalTime;
                typeCount += pd->count;
                if (typeMinTime > pd->minTime)
                    typeMinTime = pd->minTime;
                if (typeMaxTime < pd->maxTime)
                    typeMaxTime = pd->maxTime;
                float perc = (float)pd->totalTime / fullTickTotalTime * 100.0f;
                float time = (float)pd->totalTime / fullTickCount / 1000.0f;
                float minTime = (float)pd->minTime / 1000.0f;
                float maxTime = (float)pd->maxTime / 1000.0f;
                float avg = (float)pd->totalTime / (float)pd->count / 1000.0f;
                float amount = (float)pd->count / fullTickCount;
                std::string disName = name;
                if (!fullStack && disName.find("|") != std::string::npos)
                    disName = disName.substr(0, disName.find("|")) + "]";
                if (perc >= 0.1f || avg >= 0.25f || pd->maxTime > 1000)
                {
                    sink(Acore::StringFormat(
                        "{:7.3f}% {:9.3f}ms | {:7.1f} .. {:7.1f} ({:10.3f} of {:10.2f}) - {:6}    : {}",
                        perc, time, minTime, maxTime, avg, amount, key.c_str(),
                        disName.c_str()));
                }
            }
            if (i->first != PERF_MON_TOTAL)
            {
                float tPerc = (float)typeTotalTime / (float)fullTickTotalTime * 100.0f;
                float tTime = (float)typeTotalTime / fullTickCount / 1000.0f;
                float tMinTime = (float)typeMinTime / 1000.0f;
                float tMaxTime = (float)typeMaxTime / 1000.0f;
                float tAvg = (float)typeTotalTime / (float)typeCount / 1000.0f;
                float tAmount = (float)typeCount / fullTickCount;
                sink(Acore::StringFormat(
                    "{:7.3f}% {:9.3f}ms | {:7.1f} .. {:7.1f} ({:10.3f} of {:10.2f}) - {:6}    : {}",
                    tPerc, tTime, tMinTime, tMaxTime, tAvg, tAmount, key.c_str(),
                    "Total"));
            }
            sink(" ");
        }
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

PerfMonitorOperation::PerfMonitorOperation(PerformanceData* data, std::string const name,
                                                         PerformanceStack* stack)
    : data(data), name(name), stack(stack)
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

    if (stack)
    {
        stack->erase(std::remove(stack->begin(), stack->end(), name), stack->end());
    }

    delete this;
}
