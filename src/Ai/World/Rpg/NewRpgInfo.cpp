#include "NewRpgInfo.h"

#include <cmath>

#include "Timer.h"

void NewRpgInfo::ChangeToGoGrind(WorldPosition pos)
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = GoGrind{pos};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_GO_GRIND);
}

void NewRpgInfo::ChangeToGoCamp(WorldPosition pos)
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = GoCamp{pos};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_GO_CAMP);
}

void NewRpgInfo::ChangeToWanderNpc()
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = WanderNpc{};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_WANDER_NPC);
}

void NewRpgInfo::ChangeToWanderRandom()
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = WanderRandom{};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_WANDER_RANDOM);
}

void NewRpgInfo::ChangeToDoQuest(uint32 questId, const Quest* quest)
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    DoQuest do_quest;
    do_quest.questId = questId;
    do_quest.quest = quest;
    data = do_quest;
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_DO_QUEST);
}

void NewRpgInfo::ChangeToTravelFlight(ObjectGuid fromFlightMaster, std::vector<uint32> path)
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    TravelFlight flight;
    flight.fromFlightMaster = fromFlightMaster;
    flight.path = std::move(path);
    flight.inFlight = false;
    data = flight;
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_TRAVEL_FLIGHT);
}

void NewRpgInfo::ChangeToOutdoorPvp(ObjectGuid::LowType capturePointSpawnId)
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    OutdoorPvP pvp;
    pvp.capturePointSpawnId = capturePointSpawnId;
    data = pvp;
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_OUTDOOR_PVP);
}

void NewRpgInfo::ChangeToRest()
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = Rest{};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_REST);
}

void NewRpgInfo::ChangeToIdle()
{
    NewRpgStatus oldStatus = GetStatus();
    startT = getMSTime();
    data = Idle{};
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_IDLE);
}

bool NewRpgInfo::CanChangeTo(NewRpgStatus)
{
    return true;
}

void NewRpgInfo::Reset()
{
    NewRpgStatus oldStatus = GetStatus();
    data = Idle{};
    startT = getMSTime();
    if (_onStatusChanged)
        _onStatusChanged(oldStatus, RPG_IDLE);
}

void NewRpgInfo::SetMoveFarTo(WorldPosition pos)
{
    nearestMoveFarDis = FLT_MAX;
    stuckTs = 0;
    stuckAttempts = 0;
    moveFarPos = pos;
}

NewRpgStatus NewRpgInfo::GetStatus()
{
    return std::visit([](auto&& arg) -> NewRpgStatus {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Idle>) return RPG_IDLE;
        if constexpr (std::is_same_v<T, GoGrind>) return RPG_GO_GRIND;
        if constexpr (std::is_same_v<T, GoCamp>) return RPG_GO_CAMP;
        if constexpr (std::is_same_v<T, WanderNpc>) return RPG_WANDER_NPC;
        if constexpr (std::is_same_v<T, WanderRandom>) return RPG_WANDER_RANDOM;
        if constexpr (std::is_same_v<T, Rest>) return RPG_REST;
        if constexpr (std::is_same_v<T, DoQuest>) return RPG_DO_QUEST;
        if constexpr (std::is_same_v<T, TravelFlight>) return RPG_TRAVEL_FLIGHT;
        if constexpr (std::is_same_v<T, OutdoorPvP>) return RPG_OUTDOOR_PVP;
        return RPG_IDLE;
    }, data);
}

std::string NewRpgInfo::ToString()
{
    std::stringstream out;
    out << "Status: ";
    std::visit([&out, this](auto&& arg)
    {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, GoGrind>)
        {
            out << "GO_GRIND";
            out << "\nGrindPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoGrind: " << startT;
        }
        else if constexpr (std::is_same_v<T, GoCamp>)
        {
            out << "GO_CAMP";
            out << "\nCampPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastGoCamp: " << startT;
        }
        else if constexpr (std::is_same_v<T, WanderNpc>)
        {
            out << "WANDER_NPC";
            out << "\nnpcOrGoEntry: " << arg.npcOrGo.GetCounter();
            out << "\nlastWanderNpc: " << startT;
            out << "\nlastReachNpcOrGo: " << arg.lastReach;
        }
        else if constexpr (std::is_same_v<T, WanderRandom>)
        {
            out << "WANDER_RANDOM";
            out << "\nlastWanderRandom: " << startT;
        }
        else if constexpr (std::is_same_v<T, Idle>)
        {
            out << "IDLE";
        }
        else if constexpr (std::is_same_v<T, Rest>)
        {
            out << "REST";
            out << "\nlastRest: " << startT;
        }
        else if constexpr (std::is_same_v<T, DoQuest>)
        {
            out << "DO_QUEST";
            out << "\nquestId: " << arg.questId;
            out << "\nobjectiveIdx: " << arg.objectiveIdx;
            out << "\npoiPos: " << arg.pos.GetMapId() << " " << arg.pos.GetPositionX() << " "
                << arg.pos.GetPositionY() << " " << arg.pos.GetPositionZ();
            out << "\nlastReachPOI: " << (arg.lastReachPOI ? GetMSTimeDiffToNow(arg.lastReachPOI) : 0);
        }
        else if constexpr (std::is_same_v<T, TravelFlight>)
        {
            out << "TRAVEL_FLIGHT";
            out << "\nfromFlightMaster: " << arg.fromFlightMaster.GetEntry();
            out << "\nfromNode: " << arg.path[0];
            out << "\ntoNode: " << arg.path[arg.path.size() - 1];
            out << "\ninFlight: " << arg.inFlight;
        }
        else if constexpr (std::is_same_v<T, OutdoorPvP>)
        {
            out << "OUTDOOR_PVP";
            if (!arg.capturePointSpawnId)
                out << "\nNo capture point assigned.";
            else
                out << "\ncapturePointSpawnId: " << arg.capturePointSpawnId;
        }
        else
            out << "UNKNOWN";
    }, data);
    return out.str();
}
