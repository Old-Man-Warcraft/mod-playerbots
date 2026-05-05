/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef _PLAYERBOT_PERFORMANCEMONITOR_H
#define _PLAYERBOT_PERFORMANCEMONITOR_H

#include <chrono>
#include <ctime>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

typedef std::vector<std::string> PerformanceStack;

struct PerformanceData
{
    uint64_t minTime;
    uint64_t maxTime;
    uint64_t totalTime;
    uint32_t count;
    std::mutex lock;
};

struct PerfMonitorStatSnapshot
{
    uint64_t minTime{0};
    uint64_t maxTime{0};
    uint64_t totalTime{0};
    uint32_t count{0};
};

enum PerformanceMetric
{
    PERF_MON_TRIGGER,
    PERF_MON_VALUE,
    PERF_MON_ACTION,
    PERF_MON_RNDBOT,
    PERF_MON_TOTAL
};

struct PerfMetricRecord
{
    PerformanceMetric metric{PERF_MON_TOTAL};
    std::string name;
    std::string stackName;
    uint64_t elapsedUs{0};
    PerfMonitorStatSnapshot aggregate;
};

struct PerfMonitorReportRow
{
    PerformanceMetric metric{PERF_MON_TOTAL};
    std::string metricLabel;
    std::string name;
    PerfMonitorStatSnapshot stats;
    double percentage{0.0};
    double totalSeconds{0.0};
    double perTickMilliseconds{0.0};
    double minMilliseconds{0.0};
    double maxMilliseconds{0.0};
    double averageMilliseconds{0.0};
    double amount{0.0};
    bool isSummary{false};
};

struct PerfMonitorReport
{
    bool perTick{false};
    bool fullStack{false};
    uint64_t referenceTotalTime{0};
    double referenceCount{0.0};
    std::vector<PerfMonitorReportRow> rows;
};

class PerfMonitorOperation
{
public:
    PerfMonitorOperation(PerformanceData* data, PerformanceMetric metric,
                         std::string const name, std::string const stackName,
                         PerformanceStack* stack);
    void finish();

private:
    PerformanceData* data;
    PerformanceMetric metric;
    std::string const name;
    std::string const stackName;
    PerformanceStack* stack;
    std::chrono::microseconds started;
};

class PerfMonitor
{
public:
    static PerfMonitor& instance()
    {
        static PerfMonitor instance;

        return instance;
    }

    PerfMonitorOperation* start(PerformanceMetric metric, std::string const name,
                                       PerformanceStack* stack = nullptr);
    PerfMonitorReport BuildReport(bool perTick = false, bool fullStack = false);
    void PrintStats(bool perTick = false, bool fullStack = false);
    void Reset();

private:
    PerfMonitor() = default;
    virtual ~PerfMonitor() = default;

    PerfMonitor(const PerfMonitor&) = delete;
    PerfMonitor& operator=(const PerfMonitor&) = delete;

    PerfMonitor(PerfMonitor&&) = delete;
    PerfMonitor& operator=(PerfMonitor&&) = delete;

    std::map<PerformanceMetric, std::map<std::string, PerformanceData*> > data;
    std::mutex lock;
};

#define sPerfMonitor PerfMonitor::instance()

#endif
