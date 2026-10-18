#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <ShlObj.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <cmath>
#include "PluginAPI.h"
#include "minhook/include/MinHook.h"

constexpr std::uint32_t kRuntime = (1u<<24)|(16u<<16)|(244u<<4);
extern "C" __declspec(dllexport) SFSEPluginVersionData SFSEPlugin_Version = {
    1, 65, "Enhanced Vehicles", "Local vehicle investigation", 0, 0,
    {kRuntime, 0}, 0, 0, 0
};

static std::uintptr_t image;
static HANDLE output = INVALID_HANDLE_VALUE;
static SRWLOCK writeLock = SRWLOCK_INIT;
static std::atomic<unsigned> events{0};
static thread_local bool writing = false;
static std::atomic<unsigned> selectHits{0}, snapHits{0}, exitHits{0};
// Opt-in experiment limited to the civil ATV extra seats. Explicit parked
// orders hold while their seat keyword remains; clearing it requests release.
static bool passengerExperiment = false;
static thread_local bool passengerCall = false;
using PassengerFn = void(*)(std::uint32_t,std::uint32_t,bool,int);
static PassengerFn passengerOriginal;
struct PassengerRecord {void* actor{};void* furniture{};std::uint32_t handle{};int marker{-1};bool exiting{};ULONGLONG exitTime{};bool queuedExit{};ULONGLONG exitDue{};bool manualOrder{};bool holdLogged{};};
static PassengerRecord tracked[8];
static SRWLOCK passengerLock=SRWLOCK_INIT;
static void(*releaseReferenceOriginal)(void*)=nullptr;
static void RetainReference(void* reference) {
    if(reference) InterlockedExchangeAdd64(reinterpret_cast<volatile LONG64*>(static_cast<unsigned char*>(reference)+8),0x80000200001LL);
}
static void ReleaseReference(void* reference) {
    if(reference && releaseReferenceOriginal) releaseReferenceOriginal(reference);
}
static PassengerRecord SnapshotPassenger(bool remove=false,void* actor=nullptr) {
    AcquireSRWLockExclusive(&passengerLock);
    PassengerRecord result{};
    for(auto& entry:tracked) {
        if(entry.actor && (!actor || entry.actor==actor)) {
            result=entry;
            if(remove) entry={};
            else {RetainReference(result.actor);RetainReference(result.furniture);}
            break;
        }
    }
    ReleaseSRWLockExclusive(&passengerLock);
    return result;
}
static PassengerRecord SnapshotSlot(unsigned slot) {
    AcquireSRWLockExclusive(&passengerLock);
    auto result=slot<8 ? tracked[slot] : PassengerRecord{};
    RetainReference(result.actor);RetainReference(result.furniture);
    ReleaseSRWLockExclusive(&passengerLock);
    return result;
}
static void ReleaseRecord(PassengerRecord record) {ReleaseReference(record.actor);ReleaseReference(record.furniture);}
static bool TrackPassenger(void* actor,void* furniture,std::uint32_t handle,int marker,bool manualOrder=false) {
    if(!actor || !furniture || !handle || (marker!=1 && marker!=3 && marker!=4)) return false;
    AcquireSRWLockExclusive(&passengerLock);
    PassengerRecord* free=nullptr;
    bool conflict=false;
    for(auto& entry:tracked) {
        if(!entry.actor) {if(!free) free=&entry;continue;}
        if(entry.actor==actor || (entry.furniture==furniture && entry.marker==marker)) conflict=true;
    }
    bool allowed=free && !conflict;
    if(allowed) {RetainReference(actor);RetainReference(furniture);*free={actor,furniture,handle,marker};free->manualOrder=manualOrder;}
    ReleaseSRWLockExclusive(&passengerLock);
    return allowed;
}
static bool BeginPassengerExit(void* actor) {
    AcquireSRWLockExclusive(&passengerLock);
    bool start=false;
    for(auto& entry:tracked) if(entry.actor==actor && actor && !entry.exiting) {
        entry.exiting=true;entry.exitTime=GetTickCount64();start=true;break;
    }
    ReleaseSRWLockExclusive(&passengerLock);
    return start;
}

static unsigned ExitDelay(int marker) {return marker==3 ? 450u : 950u;}
static bool QueuePassengerExit(void* actor,ULONGLONG now) {
    AcquireSRWLockExclusive(&passengerLock);
    bool accepted=false;
    for(auto& entry:tracked) if(entry.actor==actor && actor) {
        if(!entry.exiting) {
            entry.exiting=true;entry.exitTime=now;
            entry.queuedExit=true;entry.exitDue=now+ExitDelay(entry.marker);
        }
        accepted=true;break;
    }
    ReleaseSRWLockExclusive(&passengerLock);
    return accepted;
}
static bool TakeQueuedExit(void* actor,ULONGLONG now) {
    AcquireSRWLockExclusive(&passengerLock);
    bool ready=false;
    for(auto& entry:tracked) if(entry.actor==actor && actor && entry.queuedExit && now>=entry.exitDue) {
        entry.queuedExit=false;ready=true;break;
    }
    ReleaseSRWLockExclusive(&passengerLock);
    return ready;
}

template<class T> T Read(const void* pointer, std::size_t offset) {
    if (!pointer) return T{};
    __try {return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(pointer)+offset);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return T{};}
}
static bool Interesting(void* actor) {
    if(!actor) return false;
    const auto id=Read<std::uint32_t>(actor,0x28);
    if(id==0x1631C5 || id==0xFE00B007 || id==0x14) return true;
    auto record=SnapshotPassenger(false,actor);
    bool active=record.actor!=nullptr;
    ReleaseRecord(record);
    return active;
}
static std::uint32_t ReferenceHandle(void* reference) {
    if(!reference || !image || !Read<std::uint32_t>(reference,0x24)) return 0;
    std::uint32_t result=0;
    // Reproduce the synchronous callback used at 018F6B33..018F6B6A.
    struct Context {std::uint32_t* output; void* expected;} context{&result,reference};
    struct Callback {void* vtable; Context* context;} callback{reinterpret_cast<void*>(image+0x4AF8BE0),&context};
    using LookupFn=void(*)(void*,std::uint32_t,void*);
    auto lookup=reinterpret_cast<LookupFn>(image+0x28CCC40);
    void* manager=Read<void*>(reinterpret_cast<void*>(image+0x5E60380),0);
    if(!manager) return 0;
    lookup(manager,Read<std::uint32_t>(reference,0x24),&callback);
    return result;
}
static bool PlayerDrivesTarget(void* high,std::uint32_t handle) {
    return high && handle && Read<std::uint32_t>(high,0x428)==handle && Read<int>(high,0x4FC)==0;
}
using HasKeywordFn=bool(*)(void*,void*);
static HasKeywordFn hasKeywordOriginal;
static bool HasOrderedSeatKeyword(PassengerRecord record) {
    void* base=Read<void*>(record.furniture,0x98);
    auto count=Read<unsigned>(base,0x270);
    void* markers=Read<void*>(base,0x278);
    if(!hasKeywordOriginal || !markers || count>20 || record.marker<0 || static_cast<unsigned>(record.marker)>=count) return false;
    void* keyword=Read<void*>(markers,static_cast<size_t>(record.marker)*40+16);
    return keyword && hasKeywordOriginal(record.actor,keyword);
}
static bool MarkHoldLogged(void* actor) {
    AcquireSRWLockExclusive(&passengerLock);
    bool first=false;
    for(auto& entry:tracked) if(entry.actor==actor && actor && !entry.holdLogged) {entry.holdLogged=true;first=true;break;}
    ReleaseSRWLockExclusive(&passengerLock);
    return first;
}
// Role-based vehicle contract, independent of display name and plugin load index.
static bool NativeKeywordNamed(void* keyword,const char* expected) {
    if(!keyword || !expected || Read<unsigned char>(keyword,0x2E)!=4) return false;
    auto table=Read<void*>(keyword,0);
    if(Read<void*>(table,0x268)!=reinterpret_cast<void*>(image+0x31B850)) return false;
    __try {
        const char* name=reinterpret_cast<const char*(*)(void*)>(image+0x31B850)(keyword);
        return name && strcmp(name,expected)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
static bool (*vehicleKeywordNamed)(void*,const char*)=NativeKeywordNamed;
static void* VehicleMarkerKeyword(void* furniture,int marker) {
    auto base=Read<void*>(furniture,0x98);
    unsigned count=Read<unsigned>(base,0x270);
    if(marker<0 || count==0 || count>20 || static_cast<unsigned>(marker)>=count) return nullptr;
    return Read<void*>(Read<void*>(base,0x278),static_cast<size_t>(marker)*40+16);
}
static bool SupportedPilotVehicle(void* furniture) {
    auto base=Read<void*>(furniture,0x98);
    return (Read<unsigned>(base,0x264)&1) && vehicleKeywordNamed(VehicleMarkerKeyword(furniture,0),"VehicleDriverSeat");
}
static bool SupportedHumanMarker(void* furniture,int marker) {
    if(!SupportedPilotVehicle(furniture)) return false;
    const char* role=marker==1?"VehicleCompanionSeat":marker==3?"EVExtraHumanSeat1":marker==4?"EVExtraHumanSeat2":nullptr;
    return role && vehicleKeywordNamed(VehicleMarkerKeyword(furniture,marker),role);
}
static void Line(const char* message);
static bool ExplicitPassengerExit(void* actor) {
    if(!actor || Read<unsigned>(actor,0x28)==0x14) return false;
    auto record=SnapshotPassenger(false,actor);
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    bool matches=record.actor==actor && record.furniture && record.handle && SupportedHumanMarker(record.furniture,record.marker) &&
        Read<unsigned>(high,0x428)==record.handle && Read<int>(high,0x4FC)==record.marker;
    bool queued=matches && QueuePassengerExit(actor,GetTickCount64());
    if(queued) Line("PASSENGER_EXPLICIT_EXIT matching managed seat queued; role keywords preserved");
    ReleaseRecord(record);return queued;
}
static bool EligiblePassenger(void* actor,void* furniture,int marker,bool flag,void* player,std::uint32_t targetHandle) {
    void* playerHigh=Read<void*>(Read<void*>(player,0x228),8);
    void* actorHigh=Read<void*>(Read<void*>(actor,0x228),8);
    const auto actorID=Read<std::uint32_t>(actor,0x28);
    const auto targetBaseID=Read<std::uint32_t>(Read<void*>(furniture,0x98),0x28);
    return (actorID && actorID!=0x14) && (marker==1 || marker==3 || marker==4) && !flag &&
       actorHigh && playerHigh && SupportedHumanMarker(furniture,marker) && targetHandle &&
       Read<std::uint32_t>(actor,0x24) && Read<std::uint32_t>(player,0x28)==0x14 &&
       !Read<std::uint32_t>(actorHigh,0x428);
}
static void Line(const char* message) {
    if (output==INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time; GetLocalTime(&time);
    char line[2048];
    int length=snprintf(line,sizeof(line),"%02u:%02u:%02u.%03u tid=%lu %s\r\n",time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,GetCurrentThreadId(),message);
    if (length<0) return;
    DWORD written;
    AcquireSRWLockExclusive(&writeLock);
    WriteFile(output,line,static_cast<DWORD>(strnlen(line,sizeof(line))),&written,nullptr);
    ReleaseSRWLockExclusive(&writeLock);
}
static void Trace(const char* name,void* actor,void* target,int marker,int result,bool stack) {
    if (!Interesting(actor) || writing || events.fetch_add(1)>=300) return;
    writing=true;
    void* process=Read<void*>(actor,0x228);
    void* high=Read<void*>(process,8);
    char line[1000];
    snprintf(line,sizeof(line),"%s actor=%08X target=%08X marker=%d result=%d actorFlagsF8=%08X actorFlagsFC=%08X processE7=%u high424=%08X high428=%08X high4F8=%d high4FC=%d",name,Read<std::uint32_t>(actor,0x28),Read<std::uint32_t>(target,0x28),marker,result,Read<std::uint32_t>(actor,0xF8),Read<std::uint32_t>(actor,0xFC),Read<std::uint8_t>(process,0xE7),Read<std::uint32_t>(high,0x424),Read<std::uint32_t>(high,0x428),Read<int>(high,0x4F8),Read<int>(high,0x4FC));
    Line(line);
    if (stack) {
        void* frames[24]{}; auto count=CaptureStackBackTrace(1,24,frames,nullptr);
        std::size_t used=0; used+=snprintf(line,sizeof(line),"STACK %s actor=%08X game_RVAs=",name,Read<std::uint32_t>(actor,0x28));
        for(unsigned i=0;i<count && used<sizeof(line)-30;i++) {
            auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
            if(address>=image && address<image+0x9000000) used+=snprintf(line+used,sizeof(line)-used," %08llX",static_cast<unsigned long long>(address-image));
        }
        Line(line);
    }
    writing=false;
}

using SelectFn=int(*)(void*,void*,bool);
using SnapFn=bool(*)(void*,void*,int,bool);
using ExitFn=void(*)(void*,bool,bool,bool);
using StateFn=void(*)(void*,void*,int,bool);
using ExitEventFn=void(*)(void*,void*);
using PrepareFn=bool(*)(void*,void*,bool);
using MarkerQueryFn=std::uint32_t*(*)(void*,std::uint32_t*,int,bool);
static SelectFn selectOriginal;
static SnapFn snapOriginal;
static ExitFn exitOriginal;
static StateFn stateOriginal;
static ExitEventFn exitEventOriginal;
static PrepareFn prepareOriginal;
static MarkerQueryFn markerQueryOriginal;
struct SeatFilter {void* furniture{};int marker{-1};std::uint32_t otherOwner{};void* actor{};};
static thread_local SeatFilter seatFilter;
static std::uint32_t* MarkerQuery(void* furniture,std::uint32_t* result,int marker,bool flag) {
    if(seatFilter.furniture==furniture && seatFilter.actor && result && marker!=seatFilter.marker) {
        // Only this selection call sees other candidates as unavailable. The
        // actual reservation/occupancy data is untouched.
        *result=seatFilter.otherOwner;
        Trace("PASSENGER_GEOMETRY_SKIP",seatFilter.actor,furniture,marker,0,false);
        return result;
    }
    auto returned=markerQueryOriginal(furniture,result,marker,flag);
    if(seatFilter.furniture==furniture && seatFilter.actor)
        Trace("PASSENGER_GEOMETRY_CANDIDATE",seatFilter.actor,furniture,marker,1,false);
    return returned;
}
static bool SeatPreparationScope(PassengerRecord record,void* actor,void* high,void* playerHigh) {
    return record.actor==actor && record.actor && record.furniture && (record.marker==1 || record.marker==3 || record.marker==4) && record.handle &&
        high && playerHigh && Read<std::uint32_t>(high,0x424)==record.handle;
}
static bool PassengerExitScope(PassengerRecord record,void* playerHigh) {
    return record.actor && record.furniture && record.handle && playerHigh &&
        Read<std::uint32_t>(playerHigh,0x428)==record.handle && Read<int>(playerHigh,0x4FC)==0;
}
using GetUpFn=bool(*)(void*,void*);
static GetUpFn getUpOriginal;
static bool HoldDriverExit(void* process,void* actor,ULONGLONG now);
static void TraceDriverForcedExit(void* actor);
static bool GetUp(void* process,void* actor) {
    if(passengerExperiment && HoldDriverExit(process,actor,GetTickCount64())) return true;
    if(passengerExperiment) {
        auto record=SnapshotPassenger(false,actor);
        void* high=Read<void*>(process,8);
        bool sameSeat=record.actor && Read<std::uint32_t>(high,0x428)==record.handle && Read<int>(high,0x4FC)==record.marker;
        void* player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        void* playerHigh=Read<void*>(Read<void*>(player,0x228),8);
        bool hold=sameSeat && record.manualOrder && !record.exiting && !PlayerDrivesTarget(playerHigh,record.handle) && HasOrderedSeatKeyword(record);
        if(hold) {
            if(MarkHoldLogged(actor)) Trace("PASSENGER_PARKED_ORDER_HOLD",actor,record.furniture,record.marker,1,true);
            ReleaseRecord(record);
            return true;
        }
        bool accepted=sameSeat && QueuePassengerExit(actor,GetTickCount64());
        if(accepted && !record.exiting) Trace("PASSENGER_EXIT_QUEUED",actor,record.furniture,record.marker,ExitDelay(record.marker),false);
        ReleaseRecord(record);
        // Returning false makes 019FFB90 invoke a different exit immediately.
        if(accepted) return true;
    }
    return getUpOriginal(process,actor);
}
static void TraceAnimation(void* actor,void* high,const char* phase) {
    if(!Interesting(actor)) return;
    char line[700];
    snprintf(line,sizeof(line),"ANIMATION_PREPARATION phase=%s actor=%08X desired=%d current=%d animation4F0=%p auxiliary440=%p previousVariables330Raw=%08X,%08X,%08X,%08X pendingVariables350Raw=%08X,%08X,%08X,%08X",phase,Read<std::uint32_t>(actor,0x28),Read<int>(high,0x4F8),Read<int>(high,0x4FC),Read<void*>(high,0x4F0),Read<void*>(high,0x440),Read<std::uint32_t>(high,0x330),Read<std::uint32_t>(high,0x334),Read<std::uint32_t>(high,0x338),Read<std::uint32_t>(high,0x33C),Read<std::uint32_t>(high,0x350),Read<std::uint32_t>(high,0x354),Read<std::uint32_t>(high,0x358),Read<std::uint32_t>(high,0x35C));
    Line(line);
}
struct DriverAnimationContext {void* actor{};void* furniture{};};
static thread_local DriverAnimationContext driverAnimationContext;
static bool ManagedDriverPreparation(void* actor,void* high);
static void* ManagedDriverFurniture(void* actor,void* high);
// 00636290 returns AL and receives eleven ABI slots. Slot six is opaque:
// this runtime forwards/ignores it; preserve it rather than invent its meaning.
using AnimationCandidatesFn=bool(*)(void*,void*,unsigned,bool,bool,std::uintptr_t,void*,void*,bool,bool,bool);
static AnimationCandidatesFn animationCandidatesOriginal;
// 00EF9980 returns the caller-owned resource wrapper and takes an opaque
// collection byte. Its native lifetime management remains entirely original.
using AnimationResourcesFn=void*(*)(void*,void*,unsigned char);
static AnimationResourcesFn animationResourcesOriginal;
static void* SelectPilotResourceSource(void* actor,void* furniture,DriverAnimationContext context,void* player) {
    if(actor && context.actor==actor && furniture && context.furniture==furniture && player && player!=actor && Read<unsigned>(player,0x28)==0x14 && Read<void*>(player,0xB0)) return player;
    return actor;
}
static void* ObservePilotResources(void* outputWrapper,void* actor,unsigned char collection) {
    void* source=actor;
    auto context=driverAnimationContext;
    if(passengerExperiment && actor && context.actor==actor && context.furniture) {
        auto high=Read<void*>(Read<void*>(actor,0x228),8);
        auto furniture=ManagedDriverFurniture(actor,high);
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        source=SelectPilotResourceSource(actor,furniture,context,player);
    }
    void* result=animationResourcesOriginal(outputWrapper,source,collection);
    if(source!=actor) {
        static std::atomic<unsigned> resourceLogs{0};
        if(resourceLogs.fetch_add(1)<12) {char message[220];snprintf(message,sizeof(message),"DRIVER_RESOURCE_SELECTION actor=%08X source=%08X collection=%u wrapper=%p resource=%p",Read<unsigned>(actor,0x28),Read<unsigned>(source,0x28),collection,result,Read<void*>(outputWrapper,0));Line(message);}
    }
    return result;
}
using AnimationProviderFn=void*(*)(void*);
static AnimationProviderFn animationProviderOriginal;
static void* ObservePilotProvider(void* actor) {
    void* source=actor;
    auto context=driverAnimationContext;
    if(passengerExperiment && actor && context.actor==actor && context.furniture) {
        auto high=Read<void*>(Read<void*>(actor,0x228),8);
        auto furniture=ManagedDriverFurniture(actor,high);
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        source=SelectPilotResourceSource(actor,furniture,context,player);
    }
    auto provider=animationProviderOriginal(source);
    if(source!=actor) {
        static std::atomic<unsigned> providerLogs{0};
        if(providerLogs.fetch_add(1)<12) {char line[200];snprintf(line,sizeof(line),"DRIVER_CACHED_PROVIDER actor=%08X source=%08X provider=%p",Read<unsigned>(actor,0x28),Read<unsigned>(source,0x28),provider);Line(line);}
    }
    return provider;
}
static unsigned SelectPilotClipSet(void* outputRows) {
    constexpr std::uint64_t npc=0x0AC9864F86A619B5ULL,driver=0xB7D77FB286A619B5ULL;
    auto count=Read<unsigned>(outputRows,0);auto rows=Read<unsigned char*>(outputRows,0x10);
    if(!rows || count>20) return 0;
    unsigned changed=0;
    for(unsigned i=0;i<count;i++) {
        auto row=rows+i*32;
        if(Read<int>(row,0)!=0) continue;
        auto candidates=Read<unsigned char*>(row,0x18);auto candidateCount=Read<unsigned>(row,8);
        if(!candidates || candidateCount>128) continue;
        for(unsigned j=0;j<candidateCount;j++) {
            auto key=candidates+j*64+0x28;
            if(Read<std::uint64_t>(key,0)==npc) {memcpy(key,&driver,8);changed++;}
        }
    }
    return changed;
}
// 01AF8F10 receives a stack context and the destination animation graph.
// Context +18 is the actor used for value evaluation; the destination graph
// is the separate second argument. Restore the context even on native failure.
using VariableContextFn=bool(*)(void*,void*);
static VariableContextFn variableContextOriginal;
static bool RunPilotVariableContext(void* context,void* graph,void* source) {
    auto previous=Read<void*>(context,0x18);
    if(!context || !source || source==previous) return variableContextOriginal(context,graph);
    auto slot=reinterpret_cast<void**>(static_cast<unsigned char*>(context)+0x18);
    bool result=false;
    __try {*slot=source;result=variableContextOriginal(context,graph);}
    __finally {*slot=previous;}
    return result;
}
static bool ObservePilotVariableContext(void* context,void* graph) {
    auto actor=Read<void*>(context,0x18);void* source=actor;
    auto entry=driverAnimationContext;
    if(passengerExperiment && actor && entry.actor==actor && entry.furniture) {
        auto high=Read<void*>(Read<void*>(actor,0x228),8);
        auto furniture=ManagedDriverFurniture(actor,high);
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        source=SelectPilotResourceSource(actor,furniture,entry,player);
    }
    auto result=RunPilotVariableContext(context,graph,source);
    if(source!=actor) {char message[180];snprintf(message,sizeof(message),"DRIVER_VARIABLE_CONTEXT actor=%08X evaluationSource=%08X graph=%p result=%d",Read<unsigned>(actor,0x28),Read<unsigned>(source,0x28),graph,result);Line(message);}
    return result;
}
static bool ObserveAnimationCandidates(void* actor,void* graphs,unsigned type,bool playerCollection,bool fifth,std::uintptr_t sixth,void* furniture,void* output,bool ninth,bool tenth,bool eleventh) {
    bool managed=driverAnimationContext.actor==actor && actor && driverAnimationContext.furniture==furniture && furniture;
    if(managed && animationProviderOriginal) {
        auto high=Read<void*>(Read<void*>(actor,0x228),8);
        auto furnitureScope=ManagedDriverFurniture(actor,high);
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        auto source=SelectPilotResourceSource(actor,furnitureScope,driverAnimationContext,player);
        if(source!=actor) {
            auto provider=animationProviderOriginal(source);
            if(provider) {graphs=provider;Line("DRIVER_CANDIDATE_PROVIDER player provider passed to managed pilot selection");}
        }
    }
    bool result=animationCandidatesOriginal(actor,graphs,type,playerCollection || managed,fifth,sixth,furniture,output,ninth,tenth,eleventh);
    if(managed && result) {auto changed=SelectPilotClipSet(output);char message[140];snprintf(message,sizeof(message),"DRIVER_CLIP_SET selectedPlayerCandidates=%u",changed);Line(message);}
    if(managed) Line("DRIVER_ANIMATION_PARAMETER managed pilot candidate flag applied; effective seated pose is not established");
    return result;
}

static bool Prepare(void* process,void* actor,bool flag) {
    auto record=SnapshotPassenger(false,actor);
    void* high=Read<void*>(process,8);
    void* player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    void* playerHigh=Read<void*>(Read<void*>(player,0x228),8);
    TraceAnimation(actor,high,"before");
    bool scope=SeatPreparationScope(record,actor,high,playerHigh);
    if(scope && !record.exiting) {
        const auto owner=ReferenceHandle(player);
        auto previous=seatFilter;
        seatFilter={record.furniture,record.marker,owner,actor};
        bool ready=owner && prepareOriginal(process,actor,flag);
        seatFilter=previous;
        ready=ready && Read<int>(high,0x4F8)==record.marker;
        TraceAnimation(actor,high,"extra-after");
        Trace("PASSENGER_FULL_GEOMETRY",actor,record.furniture,record.marker,ready,true);
        // Preserve the v5 entry as an explicitly logged fallback if the model
        // provides no usable animation data for the requested marker.
        if(!ready) {
            ready=snapOriginal(actor,record.furniture,record.marker,false);
            Trace("PASSENGER_GEOMETRY_FALLBACK",actor,record.furniture,record.marker,ready,false);
        }
        ReleaseRecord(record);
        return ready;
    }
    ReleaseRecord(record);
    auto previousContext=driverAnimationContext;
    bool result=false;
    __try {
        if(ManagedDriverPreparation(actor,high)) {
            driverAnimationContext={actor,ManagedDriverFurniture(actor,high)};
        }
        result=prepareOriginal(process,actor,flag);
    } __finally {driverAnimationContext=previousContext;}
    TraceAnimation(actor,high,"normal-after");
    return result;
}
static void FinishPassenger(PassengerRecord record) {
    if(!record.actor) return;
    if(Read<void*>(record.actor,0x228)) {
        void* interaction=nullptr;
        using GetInteractionFn=void(*)(void*,void**);
        void* extraList=Read<void*>(record.actor,0xC8);
        if(extraList) reinterpret_cast<GetInteractionFn>(image+0x531E20)(extraList,&interaction);
        if(interaction && Read<void*>(interaction,0)==reinterpret_cast<void*>(image+0x4C411E0) &&
           Read<std::uint32_t>(interaction,0x14)==record.handle) {
            Trace("PASSENGER_EXIT_REQUEST",record.actor,record.furniture,record.marker,2,true);
            using EndInteractionFn=void(*)(void**,int);
            reinterpret_cast<EndInteractionFn>(image+0x1A9CC40)(&interaction,2);
            Trace("PASSENGER_EXIT_RETURN",record.actor,record.furniture,record.marker,0,false);
        }
        if(interaction) {
            auto count=InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(interaction)+8),-1);
            if(count==1) {
                auto destroy=Read<void*>(Read<void*>(interaction,0),0);
                reinterpret_cast<void(*)(void*,unsigned)>(destroy)(interaction,1);
            }
        }
    }
    ReleaseRecord(record);
}
static int Select(void* furniture,void* actor,bool flag) {
    if(selectHits.fetch_add(1)<2) {char message[160];snprintf(message,sizeof(message),"SELECT_INVOKED actorFormID=%08X actorNativeHandle=%08X",Read<std::uint32_t>(actor,0x28),Read<std::uint32_t>(actor,0x24));Line(message);}
    int result=selectOriginal(furniture,actor,flag);
    Trace("SELECT_RETURN",actor,furniture,result,result,false);
    return result;
}
struct VehiclePose {float x{},y{},z{},yaw{};bool valid{};};
#include "convoy-trail.inc"
#include "boost-flight-test.inc"
struct ForwardBoostPulse {ULONGLONG until{},cooldownUntil{};unsigned pulses{};VehiclePose origin{};};
struct VehicleActionTest {unsigned mode{};unsigned stage{};ULONGLONG requestedAt{},sampleTime{},until{};VehiclePose sample{},origin{};};
struct DriverProbeRecord {void* actor{};void* furniture{};unsigned handle{};ULONGLONG deadline{};unsigned exitRequests{};bool motionArmed{};bool motionApplied{};bool motionCancelled{};ULONGLONG lastMotion{};VehiclePose baseline{};bool calibrated{};float forwardYawSign{};float steeringSign{};ULONGLONG lastFollowLog{};ULONGLONG started{};bool convoy{};int appliedLights{-1};unsigned calibrationAttempts{};VehiclePose motionPose{};ULONGLONG motionPoseTime{};VehiclePose stallPose{};ULONGLONG stallTime{};ULONGLONG assistUntil{};unsigned assistAttempts{};bool reverseTried{};ULONGLONG reverseUntil{};VehiclePose reverseOrigin{};ForwardBoostPulse boost{};VehicleActionTest actionTest{};BoostFlightTest flightTest{};bool verticalPolicy{};ULONGLONG verticalPolicyTime{};bool verticalUsed{};ULONGLONG verticalUntil{};VehiclePose verticalOrigin{};ULONGLONG fireUntil{},lastFireRequest{};VehiclePose fireOrigin{};};
static SRWLOCK driverLock=SRWLOCK_INIT;
static DriverProbeRecord driverProbe;
// Owned keyword queries are a heartbeat from the fleet quest. Editor ID and
// exact keyword getter are checked, so load-order IDs are not used as identity.
static std::atomic<void*> convoyActor{nullptr};
static std::atomic<ULONGLONG> convoyHeartbeat{0},lightsHeartbeat{0};
static std::atomic<bool> convoyLights{false};
static std::atomic<ULONGLONG> smoothHeartbeat{0};
static std::atomic<float> accelerationSeconds{2.0f};
static std::atomic<bool> smoothRelease{false};
static bool SmoothLease(ULONGLONG now) {auto t=smoothHeartbeat.load();return t && now>=t && now-t<6000;}
static VehiclePose ReadVehiclePose(void* reference);
static bool ObserveFleetKeyword(void* actor,void* keyword) {
    bool present=hasKeywordOriginal(actor,keyword);
    unsigned actionLocal=Read<unsigned>(keyword,0x28)&0xFFFFFF;
    if(present && actionLocal==0x825 && Read<unsigned char>(actor,0x2E)==0x4B && NativeKeywordNamed(keyword,"EVBoostFlightTest")) {
        AcquireSRWLockExclusive(&driverLock);
        if(driverProbe.actor==actor && driverProbe.convoy && !driverProbe.motionCancelled && !driverProbe.actionTest.mode && !driverProbe.flightTest.requested) {
            driverProbe.flightTest={};driverProbe.flightTest.requested=GetTickCount64();
            Line("BOOST_FLIGHT_TEST queued; relative height only; no horizontal input");
        }
        ReleaseSRWLockExclusive(&driverLock);
    }
    if(present && actionLocal==0x824 && Read<unsigned char>(actor,0x2E)==0x4B && NativeKeywordNamed(keyword,"EVReleasePassenger")) ExplicitPassengerExit(actor);
    if(present && (actionLocal==0x81B || actionLocal==0x81C) && Read<unsigned char>(actor,0x2E)==0x4B &&
       NativeKeywordNamed(keyword,actionLocal==0x81B?"RV8FleetMountedActionTest":"RV8FleetVerticalActionTest")) {
        bool queued=false;
        AcquireSRWLockExclusive(&driverLock);
        if(driverProbe.actor==actor && driverProbe.convoy && !driverProbe.actionTest.mode && !driverProbe.motionCancelled) {
            driverProbe.actionTest={};driverProbe.actionTest.mode=actionLocal==0x81B?1:2;
            driverProbe.actionTest.requestedAt=GetTickCount64();queued=true;
        }
        ReleaseSRWLockExclusive(&driverLock);
        if(queued) Line(actionLocal==0x81B?"VEHICLE_ACTION_TEST queued mounted trigger":"VEHICLE_ACTION_TEST queued vertical boost");
    }
    unsigned policyLocal=Read<unsigned>(keyword,0x28)&0xFFFFFF;
    if((policyLocal==0x81D || policyLocal==0x81E) && Read<unsigned char>(actor,0x2E)==0x4B &&
       NativeKeywordNamed(keyword,policyLocal==0x81D?"RV8FleetVerticalRecovery":"RV8FleetForwardSupportFire")) {
        bool fired=false;
        auto now=GetTickCount64();
        AcquireSRWLockExclusive(&driverLock);
        if(driverProbe.actor==actor && driverProbe.convoy && !driverProbe.motionCancelled) {
            if(policyLocal==0x81D) {driverProbe.verticalPolicy=present;driverProbe.verticalPolicyTime=now;}
            else if(!present) driverProbe.fireUntil=0;
            else if(driverProbe.calibrated && (!driverProbe.lastFireRequest || now-driverProbe.lastFireRequest>=1500)) {
                driverProbe.fireUntil=now+200;driverProbe.lastFireRequest=now;
                driverProbe.fireOrigin=ReadVehiclePose(driverProbe.furniture);fired=true;
            }
        }
        ReleaseSRWLockExclusive(&driverLock);
        if(fired) Line("FORWARD_SUPPORT_FIRE bounded request accepted from controller target checks");
    }
    unsigned local=Read<unsigned>(keyword,0x28)&0xFFFFFF;
    bool player=Read<unsigned>(actor,0x28)==0x14;
    bool smooth=player && local>=0x811 && local<=0x817;
    if((!smooth && (local!=0x80B && local!=0x80C || player)) || Read<unsigned char>(actor,0x2E)!=0x4B) return present;
    auto table=Read<void*>(keyword,0);
    if(Read<void*>(table,0x268)!=reinterpret_cast<void*>(image+0x31B850)) return present;
    const char* name=reinterpret_cast<const char*(*)(void*)>(image+0x31B850)(keyword);
    __try {
        if(smooth && name) {
            if(local==0x811 && strcmp(name,"RV8GradualAcceleration")==0) {
                bool first=present && !smoothHeartbeat.load();smoothHeartbeat.store(present?GetTickCount64():0);
                if(first) Line("PLAYER_ACCELERATION_SIGNAL enabled");
            } else if(local>=0x812 && local<=0x816 && present) {
                char expected[80];snprintf(expected,sizeof(expected),"RV8AccelerationSeconds%u",local-0x811);
                if(strcmp(name,expected)==0) accelerationSeconds.store(static_cast<float>(local-0x811));
            } else if(local==0x817 && strcmp(name,"RV8SmoothThrottleRelease")==0) smoothRelease.store(present);
        } else if(local==0x80B && name && strcmp(name,"RV8FleetConvoyOrder")==0) {
            if(present) {
                bool first=convoyActor.load()!=actor || !convoyHeartbeat.load();
                convoyActor.store(actor);convoyHeartbeat.store(GetTickCount64());
                if(first) Line("CONVOY_SIGNAL accepted owned driver heartbeat");
            }
            else if(convoyActor.load()==actor) {convoyHeartbeat.store(0);convoyActor.store(nullptr);ClearConvoyTrail();}
        } else if(local==0x80C && name && strcmp(name,"RV8FleetSyncLights")==0 && convoyActor.load()==actor) {
            convoyLights.store(present);lightsHeartbeat.store(GetTickCount64());
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return present;
}
static bool ConvoyLease(void* actor,ULONGLONG now) {
    auto heartbeat=convoyHeartbeat.load();
    return actor && convoyActor.load()==actor && heartbeat && now>=heartbeat && now-heartbeat<6000;
}
static SRWLOCK leadLock=SRWLOCK_INIT;
static void* leadVehicle=nullptr;
static ULONGLONG leadSeen=0;
using LookupFormFn=void*(*)(unsigned);
static LookupFormFn inputFormLookup;
static void* ResolveInputReference(unsigned id) {
    if(!id || !inputFormLookup) return nullptr;
    auto form=inputFormLookup(id);
    if(!form || Read<unsigned>(form,0x28)!=id) return nullptr;
    auto method=Read<void*>(Read<void*>(form,0),0x230);
    return method?reinterpret_cast<void*(*)(void*)>(method)(form):nullptr;
}
static void RememberLeadVehicle(void* reference,void* playerHigh,ULONGLONG now) {
    if(!reference || !Read<unsigned>(reference,0x28) || !Read<void*>(reference,0xB0) ||
       !PlayerDrivesTarget(playerHigh,ReferenceHandle(reference))) return;
    // v59: direct pursuit restored; route capture is disabled.
    AcquireSRWLockExclusive(&leadLock);
    if(leadVehicle!=reference) {RetainReference(reference);ReleaseReference(leadVehicle);leadVehicle=reference;}
    bool first=!leadSeen;
    leadSeen=now;
    ReleaseSRWLockExclusive(&leadLock);
    if(first) {char line[100];snprintf(line,sizeof(line),"LEAD_REFERENCE bound from live input vehicle=%08X",Read<unsigned>(reference,0x28));Line(line);}
}
static void* SnapshotLead(ULONGLONG now) {
    AcquireSRWLockShared(&leadLock);
    auto result=leadVehicle && now>=leadSeen && now-leadSeen<2000?leadVehicle:nullptr;
    RetainReference(result);ReleaseSRWLockShared(&leadLock);return result;
}
using HeadlightsFn=void(*)(bool,void*,void*,void*,void*);
static HeadlightsFn headlightsOriginal;
static std::atomic<int> leadLights{-1};
static std::atomic<unsigned> leadLightsVehicle{0};
static void ObserveHeadlights(bool enabled,void* lights,void* reference,void* onEffects,void* offEffects) {
    headlightsOriginal(enabled,lights,reference,onEffects,offEffects);
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto high=Read<void*>(Read<void*>(player,0x228),8);
    if(reference && PlayerDrivesTarget(high,ReferenceHandle(reference))) {
        leadLightsVehicle.store(Read<unsigned>(reference,0x28));leadLights.store(enabled?1:0);
        char line[120];snprintf(line,sizeof(line),"CONVOY_LEAD_LIGHTS vehicle=%08X enabled=%d",Read<unsigned>(reference,0x28),enabled);Line(line);
    }
}
static bool DriverHoldScope(const DriverProbeRecord& record,void* actor,void* high,ULONGLONG now) {
    return actor && record.actor==actor && record.handle && record.furniture && high &&
        now<record.deadline && Read<unsigned>(actor,0x28)!=0x14 &&
        Read<unsigned>(high,0x428)==record.handle && Read<int>(high,0x4FC)==0;
}
static void DriverExitLog(const char* phase,void* actor,unsigned handle,unsigned request) {
    char message[500];
    size_t used=snprintf(message,sizeof(message),"DRIVER_EXIT phase=%s actor=%08X vehicleHandle=%08X request=%u stackRVAs=",phase,Read<unsigned>(actor,0x28),handle,request);
    void* frames[16]{};auto count=CaptureStackBackTrace(1,16,frames,nullptr);
    for(unsigned i=0;i<count && used<sizeof(message)-20;++i) {
        auto address=reinterpret_cast<std::uintptr_t>(frames[i]);
        if(address>=image && address<image+0x4000000) used+=snprintf(message+used,sizeof(message)-used," %08llX",static_cast<unsigned long long>(address-image));
    }
    Line(message);
}
static bool HoldDriverExit(void* process,void* actor,ULONGLONG now) {
    AcquireSRWLockExclusive(&driverLock);
    auto record=driverProbe;
    bool hold=DriverHoldScope(record,actor,Read<void*>(process,8),now);
    unsigned requests=hold?++driverProbe.exitRequests:0;
    ReleaseSRWLockExclusive(&driverLock);
    if(hold && requests<=8) DriverExitLog("getup-held",actor,record.handle,requests);
    return hold;
}
static void TraceDriverForcedExit(void* actor) {
    AcquireSRWLockShared(&driverLock);
    auto record=driverProbe;
    bool log=record.actor && record.actor==actor;
    ReleaseSRWLockShared(&driverLock);
    if(log) DriverExitLog("forced-exit-observed",actor,record.handle,record.exitRequests);
}

static bool RestoreDriverCalibration(DriverProbeRecord& record);
static bool StartDriverObservation(void* actor,void* furniture,unsigned handle) {
    AcquireSRWLockExclusive(&driverLock);
    bool allowed=!driverProbe.actor;
    if(allowed) {RetainReference(actor);RetainReference(furniture);driverProbe={actor,furniture,handle,GetTickCount64()+30000};driverProbe.started=GetTickCount64();driverProbe.convoy=ConvoyLease(actor,driverProbe.started);if(driverProbe.convoy && RestoreDriverCalibration(driverProbe)) Line("DRIVER_CALIBRATION restored for this live vehicle");Line(driverProbe.convoy?"DRIVER_MODE continuous convoy":"DRIVER_MODE bounded probe");}
    ReleaseSRWLockExclusive(&driverLock);
    return allowed;
}
using VehicleTransitionFn=int(*)(void*,void*,void*);
static VehicleTransitionFn vehicleTransitionOriginal;
static bool DriverSubgraphScope(const DriverProbeRecord& record,void* actor,void* vehicle,void* high,ULONGLONG now) {
    return record.actor==actor && actor && record.furniture==vehicle && vehicle && record.handle && high &&
        now<record.deadline && Read<unsigned>(actor,0x28)!=0x14 &&
        SupportedPilotVehicle(vehicle) &&
        ((Read<unsigned>(high,0x424)==record.handle && Read<int>(high,0x4F8)==0) ||
         (Read<unsigned>(high,0x428)==record.handle && Read<int>(high,0x4FC)==0)) &&
        HasOrderedSeatKeyword({actor,vehicle,record.handle,0});
}
static int ScopedDriverTransition(void* context,void* actorWrapper,void* vehicleWrapper) {
    auto actor=Read<void*>(actorWrapper,0),vehicle=Read<void*>(vehicleWrapper,0);
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    AcquireSRWLockShared(&driverLock);
    auto record=driverProbe;
    bool scope=passengerExperiment && DriverSubgraphScope(record,actor,vehicle,high,GetTickCount64());
    ReleaseSRWLockShared(&driverLock);
    auto graph=scope?Read<void*>(high,0x458):nullptr;
    if(!graph) return vehicleTransitionOriginal(context,actorWrapper,vehicleWrapper);
    // 012B3BB5..012B3BFD applies the non-player companion subgraph. The
    // surrounding vehicle transition remains unchanged. Suppress that branch
    // only during a managed pilot transition and restore the retained pointer.
    auto slot=reinterpret_cast<void**>(static_cast<unsigned char*>(high)+0x458);
    int result=0;
    __try {
        *slot=nullptr;
        result=vehicleTransitionOriginal(context,actorWrapper,vehicleWrapper);
    } __finally { *slot=graph; }
    Line("DRIVER_ROLE_TRANSITION companion subgraph skipped for managed pilot; graph pointer restored");
    return result;
}

static bool ManagedDriverPreparation(void* actor,void* high) {
    AcquireSRWLockShared(&driverLock);
    auto record=driverProbe;
    bool valid=passengerExperiment && record.actor==actor && actor && high && record.furniture && record.handle &&
        GetTickCount64()<record.deadline && Read<unsigned>(actor,0x28)!=0x14 &&
        SupportedPilotVehicle(record.furniture) &&
        Read<unsigned>(high,0x424)==record.handle && (Read<int>(high,0x4F8)==-1 || Read<int>(high,0x4F8)==0) &&
        HasOrderedSeatKeyword({actor,record.furniture,record.handle,0});
    ReleaseSRWLockShared(&driverLock);
    return valid;
}
static void* ManagedDriverFurniture(void* actor,void* high) {
    if(!ManagedDriverPreparation(actor,high)) return nullptr;
    AcquireSRWLockShared(&driverLock);auto furniture=driverProbe.actor==actor?driverProbe.furniture:nullptr;ReleaseSRWLockShared(&driverLock);
    return furniture;
}

static void TickDriverMotion();
static void StopDriverMotion(void* actor);
static void StopDriverRecord(const DriverProbeRecord& record);
static bool RequestAnimatedDriverExit(const DriverProbeRecord& record) {
    auto process=Read<void*>(record.actor,0x228);
    auto high=Read<void*>(process,8);
    if(!record.actor || !record.furniture || !record.handle || !process || !getUpOriginal ||
       Read<unsigned>(high,0x428)!=record.handle || Read<int>(high,0x4FC)!=0) return false;
    auto previous=driverAnimationContext;
    bool accepted=false;
    __try {
        driverAnimationContext={record.actor,record.furniture};
        accepted=getUpOriginal(process,record.actor);
    } __finally {driverAnimationContext=previous;}
    return accepted;
}
static void ObserveDriverDeadline() {
    AcquireSRWLockExclusive(&driverLock);
    DriverProbeRecord completed{};
    auto now=GetTickCount64();
    if(driverProbe.convoy && ConvoyLease(driverProbe.actor,now)) driverProbe.deadline=now+6000;
    bool cancelled=driverProbe.actor &&
        !HasOrderedSeatKeyword({driverProbe.actor,driverProbe.furniture,driverProbe.handle,0});
    if(driverProbe.actor && (GetTickCount64()>=driverProbe.deadline || cancelled)) {completed=driverProbe;driverProbe={};}
    ReleaseSRWLockExclusive(&driverLock);
    if(completed.actor) {
      __try {
        StopDriverRecord(completed);
        auto high=Read<void*>(Read<void*>(completed.actor,0x228),8);
        if(Read<unsigned>(high,0x428)==completed.handle && Read<int>(high,0x4FC)==0) {
            Line("DRIVER_PROBE_END release requested at deadline or explicit stop");
            bool accepted=RequestAnimatedDriverExit(completed);
            char message[180];snprintf(message,sizeof(message),"DRIVER_ANIMATED_EXIT actor=%08X accepted=%d",Read<unsigned>(completed.actor,0x28),accepted);Line(message);
        }
      } __finally {
        ReleaseReference(completed.actor);ReleaseReference(completed.furniture);
      }
    }
}
// Vehicle FullPrepare deliberately clears the generic furniture animation key.
// Pilot entry therefore uses the vehicle interaction, not a fabricated key.
static bool EligibleDriverEntry(void* actor,void* furniture,int marker,bool flag,void* player) {
    auto actorHigh=Read<void*>(Read<void*>(actor,0x228),8);
    auto playerHigh=Read<void*>(Read<void*>(player,0x228),8);
    auto base=Read<void*>(furniture,0x98);
    auto driverKeyword=Read<void*>(Read<void*>(base,0x278),16);
    return passengerExperiment && !passengerCall && passengerOriginal && marker==0 && !flag &&
        Read<unsigned>(actor,0x28) && Read<unsigned>(actor,0x28)!=0x14 &&
        Read<unsigned char>(actor,0x2E)==0x4B && Read<unsigned>(actor,0x24) &&
        Read<unsigned>(player,0x28)==0x14 && actorHigh && playerHigh &&
        !Read<unsigned>(actorHigh,0x428) && SupportedPilotVehicle(furniture) &&
        Read<unsigned>(base,0x270)>=1 && (Read<unsigned>(base,0x264)&1) &&
        driverKeyword && hasKeywordOriginal && hasKeywordOriginal(actor,driverKeyword);
}
static bool RequestDriverInteraction(void* actor,void* furniture,unsigned actorHandle,unsigned targetHandle,void* playerHigh) {
    if(!actorHandle || !targetHandle || PlayerDrivesTarget(playerHigh,targetHandle) ||
       !StartDriverObservation(actor,furniture,targetHandle)) {
        Line("DRIVER_VEHICLE_REJECT handle, player occupancy or active observation");return false;
    }
    Line("DRIVER_VEHICLE_REQUEST native vehicle interaction; natural entry; release mode recorded by DRIVER_MODE");
    bool previous=passengerCall;passengerCall=true;
    passengerOriginal(actorHandle,targetHandle,false,0);
    passengerCall=previous;
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    bool bound=Read<unsigned>(high,0x428)==targetHandle && Read<int>(high,0x4FC)==0;
    char message[280];snprintf(message,sizeof(message),"DRIVER_VEHICLE_RETURN actor=%08X vehicle=%08X bound=%d desired=%d current=%d",Read<unsigned>(actor,0x28),Read<unsigned>(furniture,0x28),bound,Read<int>(high,0x4F8),Read<int>(high,0x4FC));Line(message);
    if(bound) {
        AcquireSRWLockExclusive(&driverLock);
        if(driverProbe.actor==actor && driverProbe.handle==targetHandle) driverProbe.motionArmed=true;
        ReleaseSRWLockExclusive(&driverLock);
        Line("DRIVER_MOTION_ARMED two-second steering calibration at six seconds; bounded follow after ten seconds; neutral at 24 seconds");
    }
    // No generic fallback after a partial native entry: it would overwrite
    // the interaction and hide a failed transition.
    return bound;
}
static bool EnterVehicleDriver(void* actor,void* furniture) {
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    return RequestDriverInteraction(actor,furniture,ReferenceHandle(actor),ReferenceHandle(furniture),Read<void*>(Read<void*>(player,0x228),8));
}
static bool Snap(void* actor,void* furniture,int marker,bool flag) {
    if(snapHits.fetch_add(1)<2) {char message[160];snprintf(message,sizeof(message),"SNAP_INVOKED actorFormID=%08X actorNativeHandle=%08X",Read<std::uint32_t>(actor,0x28),Read<std::uint32_t>(actor,0x24));Line(message);}
    Trace("SNAP_BEGIN",actor,furniture,marker,flag,true);
    void* player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    if(EligibleDriverEntry(actor,furniture,marker,flag,player)) return EnterVehicleDriver(actor,furniture);
    void* actorHigh=Read<void*>(Read<void*>(actor,0x228),8);
    const auto targetHandle=passengerExperiment && !passengerCall && (marker==1 || marker==3 || marker==4) ? ReferenceHandle(furniture) : 0;
    if(passengerExperiment && !passengerCall && passengerOriginal && EligiblePassenger(actor,furniture,marker,flag,player,targetHandle)) {
        const auto actorHandle=ReferenceHandle(actor);
        if(!actorHandle) {Trace("PASSENGER_EXPERIMENT_REJECT_HANDLE",actor,furniture,marker,0,false);return false;}
        if(!TrackPassenger(actor,furniture,targetHandle,marker,!PlayerDrivesTarget(Read<void*>(Read<void*>(player,0x228),8),targetHandle))) {Trace("PASSENGER_EXPERIMENT_REJECT_BUSY",actor,furniture,marker,0,false);return false;}
        passengerCall=true;
        Trace("PASSENGER_EXPERIMENT_REQUEST",actor,furniture,marker,0,true);
        passengerOriginal(actorHandle,targetHandle,false,5);
        passengerCall=false;
        actorHigh=Read<void*>(Read<void*>(actor,0x228),8);
        bool seated=Read<std::uint32_t>(actorHigh,0x428)==targetHandle && Read<int>(actorHigh,0x4FC)==marker;
        Trace("PASSENGER_EXPERIMENT_RETURN",actor,furniture,marker,seated,false);
        return seated;
    }
    bool result=snapOriginal(actor,furniture,marker,flag);
    Trace("SNAP_END",actor,furniture,marker,result,false);
    if(passengerExperiment && marker==0 && Read<unsigned>(actor,0x28)!=0x14 && SupportedPilotVehicle(furniture)) {
        auto high=Read<void*>(Read<void*>(actor,0x228),8);
        char message[300];snprintf(message,sizeof(message),"DRIVER_POSE actor=%08X vehicle=%08X snap=%d desired=%d current=%d key=%016llX auxiliary=%p",Read<unsigned>(actor,0x28),Read<unsigned>(furniture,0x28),result,Read<int>(high,0x4F8),Read<int>(high,0x4FC),static_cast<unsigned long long>(Read<std::uint64_t>(high,0x4F0)),Read<void*>(high,0x440));Line(message);
    }
    return result;
}
static bool NaturalRearScope(void* actor,void* furniture,int marker,void* player,unsigned handle) {
    if(!passengerExperiment || passengerCall || !passengerOriginal || !EligiblePassenger(actor,furniture,marker,false,player,handle)) return false;
    return Read<unsigned char>(actor,0x2E)==0x4B && HasOrderedSeatKeyword({actor,furniture,handle,marker});
}
static bool RequestNaturalRear(void* actor,void* furniture,int marker,unsigned actorHandle,unsigned targetHandle,void* playerHigh) {
    if(!actorHandle || !targetHandle || !TrackPassenger(actor,furniture,targetHandle,marker,!PlayerDrivesTarget(playerHigh,targetHandle))) return false;
    bool previous=passengerCall;passengerCall=true;
    // The outer wrapper immediately places the actor. Use the vehicle's natural
    // interaction with no final forced retry, as with the validated pilot entry.
    passengerOriginal(actorHandle,targetHandle,false,0);
    passengerCall=previous;
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    bool accepted=(Read<unsigned>(high,0x428)==targetHandle && Read<int>(high,0x4FC)==marker) || Read<unsigned>(high,0x424)==targetHandle;
    Trace("REAR_NATURAL_ENTRY",actor,furniture,marker,accepted,true);
    if(!accepted) FinishPassenger(SnapshotPassenger(true,actor));
    return accepted;
}

// The outer Papyrus wrapper immediately places occupants. Bypass it only
// for the experimental pilot; rear occupants keep their validated wrapper.
using OuterSnapFn=bool(*)(void*,void*,int);
static OuterSnapFn outerSnapOriginal;
static bool RebindSavedOccupant(void* actor,void* furniture,unsigned savedHandle,int marker) {
    auto savedHigh=Read<void*>(Read<void*>(actor,0x228),8);
    if(passengerExperiment && Read<unsigned char>(actor,0x2E)==0x4B && Read<unsigned>(actor,0x28)!=0x14 &&
       savedHandle && Read<unsigned>(savedHigh,0x428)==savedHandle && Read<int>(savedHigh,0x4FC)==marker &&
       SupportedPilotVehicle(furniture) && HasOrderedSeatKeyword({actor,furniture,savedHandle,marker})) {
        if(marker==0) {
            AcquireSRWLockShared(&driverLock);bool exists=driverProbe.actor==actor && driverProbe.furniture==furniture;ReleaseSRWLockShared(&driverLock);
            if(!exists && !StartDriverObservation(actor,furniture,savedHandle)) return false;
            AcquireSRWLockExclusive(&driverLock);if(driverProbe.actor==actor) driverProbe.motionArmed=true;ReleaseSRWLockExclusive(&driverLock);
            Line("LOAD_REBIND saved driver registered without entry replay");return true;
        }
        if(SupportedHumanMarker(furniture,marker)) {
            auto existing=SnapshotPassenger(false,actor);bool exists=existing.actor && existing.furniture==furniture && existing.marker==marker;ReleaseRecord(existing);
            if(!exists && !TrackPassenger(actor,furniture,savedHandle,marker,true)) return false;
            Line("LOAD_REBIND saved passenger registered without entry replay");return true;
        }
    }
    return false;
}
// Papyrus SnapIntoInteraction first calls 01799E50, which returns true
// immediately when the actor already occupies the target. The outer seat
// wrapper never runs in that case. Rebind before that observed early return.
using PapyrusSnapFn=bool(*)(void*,void*);
static PapyrusSnapFn papyrusSnapOriginal;
static unsigned (*savedReferenceHandle)(void*)=ReferenceHandle;
static bool ObservePapyrusSnap(void* actor,void* furniture) {
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    int marker=Read<int>(high,0x4FC);
    bool scope=passengerExperiment && actor && furniture && Read<unsigned char>(actor,0x2E)==0x4B &&
        Read<unsigned>(actor,0x28)!=0x14 && Read<unsigned>(high,0x428) &&
        SupportedPilotVehicle(furniture) && (marker==0 || SupportedHumanMarker(furniture,marker)) &&
        HasOrderedSeatKeyword({actor,furniture,0,marker});
    bool acknowledged=false;
    if(scope) {
        unsigned handle=savedReferenceHandle(furniture);
        if(handle && Read<unsigned>(high,0x428)==handle) {
            acknowledged=RebindSavedOccupant(actor,furniture,handle,marker);
            char line[220];snprintf(line,sizeof(line),"NATIVE_LOAD_REBIND_ACK actor=%08X vehicle=%08X marker=%d registered=%d",Read<unsigned>(actor,0x28),Read<unsigned>(furniture,0x28),marker,acknowledged);Line(line);
        } else scope=false;
    }
    bool originalResult=papyrusSnapOriginal(actor,furniture);
    // A successful engine no-op must not masquerade as a registered occupant.
    return scope ? originalResult && acknowledged : originalResult;
}

static bool OuterSnap(void* actor,void* furniture,int marker) {
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto savedHigh=Read<void*>(Read<void*>(actor,0x228),8);
    auto savedHandle=passengerExperiment && Read<unsigned char>(actor,0x2E)==0x4B && Read<unsigned>(savedHigh,0x428) && SupportedPilotVehicle(furniture) ? ReferenceHandle(furniture) : 0;
    if(savedHandle && RebindSavedOccupant(actor,furniture,savedHandle,Read<int>(savedHigh,0x4FC))) return true;
    if(EligibleDriverEntry(actor,furniture,marker,false,player)) return EnterVehicleDriver(actor,furniture);
    bool rearCandidate=(marker==1 || marker==3 || marker==4) && passengerExperiment && !passengerCall && Read<unsigned char>(actor,0x2E)==0x4B && Read<unsigned>(actor,0x28)!=0x14 && SupportedPilotVehicle(furniture) && HasOrderedSeatKeyword({actor,furniture,0,marker});
    auto handle=rearCandidate?ReferenceHandle(furniture):0;
    if(NaturalRearScope(actor,furniture,marker,player,handle))
        return RequestNaturalRear(actor,furniture,marker,ReferenceHandle(actor),handle,Read<void*>(Read<void*>(player,0x228),8));
    return outerSnapOriginal(actor,furniture,marker);
}

using ChangeKeywordFn=void(*)(void*,void*);
static ChangeKeywordFn addKeywordOriginal,removeKeywordOriginal;
using FollowerSelectionFn=void(*)(void*,void*,void*,bool);
static FollowerSelectionFn followerSelectionOriginal;
static void ObserveFleetAddKeyword(void* actor,void* keyword) {
    addKeywordOriginal(actor,keyword);
    ObserveFleetKeyword(actor,keyword);
}
static void ObserveFleetRemoveKeyword(void* actor,void* keyword) {
    removeKeywordOriginal(actor,keyword);
    ObserveFleetKeyword(actor,keyword);
}

static bool ProtectRearPassenger(const PassengerRecord& record,void* target,void* high) {
    return record.actor && !record.exiting && record.furniture &&
       (record.marker==1 || record.marker==3 || record.marker==4) && record.handle && high &&
       Read<unsigned>(high,0x428)==record.handle && Read<int>(high,0x4FC)==record.marker &&
       HasOrderedSeatKeyword(record);
}
static void SelectFrontFollower(void* context,void* player,void* target,bool flag) {
    // Native selection checks this exclusion before cancelling furniture.
    // Add it only during this call, only to managed passengers, and preserve
    // tags owned by another mod. No native front-seat category is added.
    void* keyword=Read<void*>(reinterpret_cast<void*>(image+0x5F043F0),0);
    PassengerRecord held[9]{};
    bool added[9]{};
    if(passengerExperiment && keyword && Read<unsigned char>(keyword,0x2E)==4) {
        for(unsigned i=0;i<8;++i) {
            held[i]=SnapshotSlot(i);
            auto high=Read<void*>(Read<void*>(held[i].actor,0x228),8);
            if(ProtectRearPassenger(held[i],target,high) && !hasKeywordOriginal(held[i].actor,keyword)) {
                addKeywordOriginal(held[i].actor,keyword);
                added[i]=hasKeywordOriginal(held[i].actor,keyword);
                Trace("FRONT_SELECTION_EXCLUDE_REAR",held[i].actor,target,held[i].marker,added[i],false);
            }
        }
    }
    if(passengerExperiment && keyword && Read<unsigned char>(keyword,0x2E)==4) {
        AcquireSRWLockShared(&driverLock);
        auto driver=driverProbe;
        RetainReference(driver.actor);RetainReference(driver.furniture);
        ReleaseSRWLockShared(&driverLock);
        held[8]={driver.actor,driver.furniture,driver.handle,0};
        auto high=Read<void*>(Read<void*>(driver.actor,0x228),8);
        if(driver.actor && driver.handle && Read<unsigned>(high,0x428)==driver.handle && Read<int>(high,0x4FC)==0 && !hasKeywordOriginal(driver.actor,keyword)) {
            addKeywordOriginal(driver.actor,keyword);
            added[8]=hasKeywordOriginal(driver.actor,keyword);
            Trace("FRONT_SELECTION_EXCLUDE_DRIVER",driver.actor,driver.furniture,0,added[8],false);
        }
    }
    followerSelectionOriginal(context,player,target,flag);
    for(unsigned i=0;i<9;++i) {
        if(added[i]) removeKeywordOriginal(held[i].actor,keyword);
        ReleaseRecord(held[i]);
    }
}

using InteractionEnterFn=int(*)(void*,void*,bool);
static InteractionEnterFn interactionEnterOriginal;
static std::atomic<unsigned> interactionObservations{0};
static int ObserveInteractionEnter(void* interaction,void* context,bool forced) {
    const auto actorHandleBefore=Read<unsigned>(interaction,0x10);
    const bool log=interactionObservations.fetch_add(1)<80;
    if(log) {
        char message[300];
        const auto table=reinterpret_cast<std::uintptr_t>(Read<void*>(interaction,0));
        const auto contextTable=reinterpret_cast<std::uintptr_t>(Read<void*>(context,0));
        snprintf(message,sizeof(message),"INTERACTION_ENTER actorHandle=%08X targetHandle=%08X forced=%d tableRVA=%llX contextTableRVA=%llX",Read<unsigned>(interaction,0x10),Read<unsigned>(interaction,0x14),forced,static_cast<unsigned long long>(table>=image?table-image:0),static_cast<unsigned long long>(contextTable>=image?contextTable-image:0));
        Line(message);
    }
    const int result=interactionEnterOriginal(interaction,context,forced);
    if(log) {char message[100];snprintf(message,sizeof(message),"INTERACTION_ENTER_RETURN actorHandle=%08X result=%d",actorHandleBefore,result);Line(message);}
    return result;
}

static void Exit(void* actor,bool a,bool b,bool c) {
    if(passengerExperiment) {
        auto seated=SnapshotPassenger(false,actor);
        if(seated.actor && !seated.exiting) {
            auto high=Read<void*>(Read<void*>(actor,0x228),8);
            char diagnostic[360];snprintf(diagnostic,sizeof(diagnostic),"PASSENGER_DIRECT_EXIT_UNORDERED actor=%08X vehicle=%08X trackedMarker=%d actualMarker=%d matchingHandle=%d rolePresent=%d manualOrder=%d flags=%d%d%d",Read<unsigned>(actor,0x28),Read<unsigned>(seated.furniture,0x28),seated.marker,Read<int>(high,0x4FC),Read<unsigned>(high,0x428)==seated.handle,HasOrderedSeatKeyword(seated),seated.manualOrder,a,b,c);Line(diagnostic);
        }
        ReleaseRecord(seated);
    }
    StopDriverMotion(actor);
    if(passengerExperiment) TraceDriverForcedExit(actor);
    if(exitHits.fetch_add(1)<2) {char message[160];snprintf(message,sizeof(message),"EXIT_INVOKED actorFormID=%08X actorNativeHandle=%08X",Read<std::uint32_t>(actor,0x28),Read<std::uint32_t>(actor,0x24));Line(message);}
    Trace("EXIT_BEGIN",actor,nullptr,-1,(a?1:0)|(b?2:0)|(c?4:0),true);
    exitOriginal(actor,a,b,c);
    Trace("EXIT_END",actor,nullptr,-1,0,false);
}
static void State(void* process,void* actor,int state,bool flag) {
    if(passengerExperiment && Read<std::uint32_t>(actor,0x28)==0x14 && state==4) {
        for(unsigned slot=0;slot<8;slot++) {
            auto passenger=SnapshotSlot(slot);
            if(PassengerExitScope(passenger,Read<void*>(process,8)) ) {
                Trace("PASSENGER_ANIMATED_EXIT_REQUEST",passenger.actor,passenger.furniture,passenger.marker,0,true);
                using GetUpFn=bool(*)(void*,void*);
                auto passengerProcess=Read<void*>(passenger.actor,0x228);
                if(passengerProcess) GetUp(passengerProcess,passenger.actor);
            }
            ReleaseRecord(passenger);
        }
    }
    bool record=state!=0 || Read<std::uint32_t>(Read<void*>(process,8),0x428)!=0;
    if(record) Trace("STATE_BEGIN",actor,nullptr,state,flag,true);
    stateOriginal(process,actor,state,flag);
    if(record) Trace("STATE_END",actor,nullptr,state,flag,false);
    if(passengerExperiment && state==0) {
        auto passenger=SnapshotPassenger(false,actor);
        bool finished=passenger.actor==actor && passenger.exiting;
        ReleaseRecord(passenger);
        if(finished) FinishPassenger(SnapshotPassenger(true,actor));
    }
}
static void ObserveDirectPhysics(void* vehicle);
class ExitWatch final : public SFSETaskInterface::ITaskDelegate {
public:
    void Run() override {
        TickDriverMotion();
        ObserveDriverDeadline();
        AcquireSRWLockShared(&driverLock);
        auto observedDriver=driverProbe;
        RetainReference(observedDriver.furniture);
        ReleaseSRWLockShared(&driverLock);
        if(observedDriver.furniture) ObserveDirectPhysics(observedDriver.furniture);
        ReleaseReference(observedDriver.furniture);
        for(unsigned slot=0;slot<8;slot++) {
            auto passenger=SnapshotSlot(slot);
            if(passenger.actor && passenger.manualOrder && !passenger.exiting && !HasOrderedSeatKeyword(passenger)) {
                void* process=Read<void*>(passenger.actor,0x228);
                void* high=Read<void*>(process,8);
                if(process && Read<std::uint32_t>(high,0x428)==passenger.handle && Read<int>(high,0x4FC)==passenger.marker) GetUp(process,passenger.actor);
                else FinishPassenger(SnapshotPassenger(true,passenger.actor));
            }
            if(passenger.actor && TakeQueuedExit(passenger.actor,GetTickCount64())) {
                void* process=Read<void*>(passenger.actor,0x228);
                void* high=Read<void*>(process,8);
                if(process && Read<std::uint32_t>(high,0x428)==passenger.handle && Read<int>(high,0x4FC)==passenger.marker) {
                    bool result=getUpOriginal(process,passenger.actor);
                    Trace("PASSENGER_EXIT_RELEASED",passenger.actor,passenger.furniture,passenger.marker,result,true);
                } else {
                    Trace("PASSENGER_EXIT_QUEUE_CANCELLED",passenger.actor,passenger.furniture,passenger.marker,0,false);
                    FinishPassenger(SnapshotPassenger(true,passenger.actor));
                }
            }
            bool timeout=passenger.actor && passenger.exiting && GetTickCount64()-passenger.exitTime>=5000;
            if(timeout) Trace("PASSENGER_EXIT_TIMEOUT",passenger.actor,passenger.furniture,passenger.marker,0,false);
            void* actor=passenger.actor;
            // Remove before releasing the snapshot so the actor remains alive.
            if(timeout) FinishPassenger(SnapshotPassenger(true,actor));
            ReleaseRecord(passenger);
        }
    }
    void Destroy() override {}
};
static ExitWatch exitWatch;
static void ExitEvent(void* furniturePtr,void* actorPtr) {
    void* actor=Read<void*>(actorPtr,0);
    void* furniture=Read<void*>(furniturePtr,0);
    Trace("FURNITURE_EVENT",actor,furniture,-1,0,true);
    exitEventOriginal(furniturePtr,actorPtr);
}

// Passive association: preserve the per-vehicle dispatch context only while
// the engine executes it. Never keep or reuse a physics component pointer.
using InputDispatchFn=void(*)(void*,void*,void*);
using InputApplyFn=void(*)(void*,void*);
static InputDispatchFn inputDispatchOriginal;
static InputApplyFn inputApplyOriginal;
static thread_local unsigned inputVehicleID=0;
static SRWLOCK inputLogLock=SRWLOCK_INIT;
struct InputLogSlot {unsigned id{};ULONGLONG last{};unsigned count{};};
static InputLogSlot inputLogs[8];
static bool InputLogDue(unsigned id,ULONGLONG now) {
    if(!id) return false;
    AcquireSRWLockExclusive(&inputLogLock);
    InputLogSlot* found=nullptr;
    for(auto& slot:inputLogs) {if(slot.id==id) {found=&slot;break;}if(!slot.id && !found) found=&slot;}
    bool due=found && found->count<40 && (!found->count || now-found->last>=1000);
    if(due) {found->id=id;found->last=now;++found->count;}
    ReleaseSRWLockExclusive(&inputLogLock);
    return due;
}
static void ObserveInputDispatch(void* manager,void* context,void* identity) {
    const auto prior=inputVehicleID;
    inputVehicleID=Read<unsigned>(identity,0);
    inputDispatchOriginal(manager,context,identity);
    inputVehicleID=prior;
}
static bool DriverObservationActive();
static void ObserveDirectPhysicsID(unsigned id);
struct AccelerationRamp {unsigned vehicle{};ULONGLONG last{};float value{};};
static AccelerationRamp playerRamp;
static std::atomic<ULONGLONG> lastAccelerationLog{0};
static SRWLOCK rampLock=SRWLOCK_INIT;
static float RampAccelerator(AccelerationRamp& ramp,unsigned vehicle,float requested,ULONGLONG now,float seconds,bool gradualRelease) {
    if(!vehicle || !std::isfinite(requested) || requested<0 || requested>1 || !std::isfinite(seconds) || seconds<1 || seconds>5) {ramp={};return requested;}
    bool reset=ramp.vehicle!=vehicle || !ramp.last || now<ramp.last || now-ramp.last>250;
    float dt=reset?0.0f:std::fmin(0.1f,static_cast<float>(now-ramp.last)/1000.0f);
    if(reset) ramp={vehicle,now,0};
    if(requested>ramp.value) ramp.value=std::fmin(requested,ramp.value+dt/seconds);
    else ramp.value=gradualRelease?std::fmax(requested,ramp.value-dt/0.5f):requested;
    ramp.last=now;return ramp.value;
}
static void CopyThrottleInput(const void* payload,float value,unsigned char* filtered) {
    memcpy(filtered,payload,0x30);memcpy(filtered+0x14,&value,4);
}
static bool FilterPlayerAccelerator(void* component,void* payload,unsigned char* filtered) {
    auto now=GetTickCount64();
    if(!payload || !component || !filtered || !inputVehicleID || !SmoothLease(now) ||
       Read<void*>(component,0)!=reinterpret_cast<void*>(image+0x4C46F98) || Read<unsigned>(component,0xC)!=inputVehicleID) return false;
    auto lead=SnapshotLead(now);
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto high=Read<void*>(Read<void*>(player,0x228),8);
    bool own=lead && Read<unsigned>(lead,0x28)==inputVehicleID && PlayerDrivesTarget(high,ReferenceHandle(lead));
    ReleaseReference(lead);
    if(!own) return false;
    float requested=Read<float>(payload,0x14),reverse=Read<float>(payload,0x18);
    // Reverse and special actions (including boost) retain the complete native input.
    if(!std::isfinite(requested) || requested<0 || requested>1 || reverse!=0 || Read<unsigned>(payload,0x1C)) {
        AcquireSRWLockExclusive(&rampLock);playerRamp={};ReleaseSRWLockExclusive(&rampLock);return false;
    }
    AcquireSRWLockExclusive(&rampLock);
    float value=RampAccelerator(playerRamp,inputVehicleID,requested,now,accelerationSeconds.load(),smoothRelease.load());
    ReleaseSRWLockExclusive(&rampLock);
    CopyThrottleInput(payload,value,filtered);return true;
}

// Diagnostic capture only: copy the existing player input, never synthesize actions.
struct ActionCaptureState {
    unsigned vehicle{}, count{};
    ULONGLONG start{}, last{};
    unsigned char prior[0x30]{};
};
static ActionCaptureState actionCapture;
static SRWLOCK actionCaptureLock=SRWLOCK_INIT;
static std::atomic<unsigned> weaponRoutingSamples{0};
static std::uintptr_t ModuleRVA(void* pointer) {
    auto value=reinterpret_cast<std::uintptr_t>(pointer);
    return value>=image && value-image<0x8000000 ? value-image : 0;
}
static void CaptureWeaponRouting(void* component,unsigned vehicle) {
    if(weaponRoutingSamples.fetch_add(1)>=12) return;
    auto holder=Read<void*>(component,0x20);
    auto weapon=Read<void*>(holder,0x20);
    auto table=Read<void*>(weapon,0);
    auto count=Read<unsigned>(weapon,0x68);
    auto entries=Read<void*>(weapon,0x70);
    char line[300];
    snprintf(line,sizeof(line),"PLAYER_WEAPON_ROUTING vehicle=%08X weapon=%p tableRva=%llX updateRva=%llX count=%u",vehicle,weapon,static_cast<unsigned long long>(ModuleRVA(table)),static_cast<unsigned long long>(ModuleRVA(Read<void*>(table,0x10))),count);Line(line);
    if(!entries || count>16) return;
    for(unsigned i=0;i<count && i<8;++i) {
        auto entry=Read<void*>(entries,i*sizeof(void*));auto vtable=Read<void*>(entry,0);
        snprintf(line,sizeof(line),"PLAYER_WEAPON_ENTRY vehicle=%08X index=%u tableRva=%llX checkRva=%llX startRva=%llX stopRva=%llX",vehicle,i,static_cast<unsigned long long>(ModuleRVA(vtable)),static_cast<unsigned long long>(ModuleRVA(Read<void*>(vtable,0x28))),static_cast<unsigned long long>(ModuleRVA(Read<void*>(vtable,0x30))),static_cast<unsigned long long>(ModuleRVA(Read<void*>(vtable,0x58))));Line(line);
    }
}
static bool ActionCaptureDue(ActionCaptureState& state,unsigned vehicle,const unsigned char* bytes,ULONGLONG now) {
    if(!vehicle || !bytes) return false;
    if(!state.start) {state.start=now;}
    if(now<state.start || now-state.start>300000 || state.count>=2000) return false;
    bool transition=state.vehicle!=vehicle || !state.count || memcmp(state.prior+0x1C,bytes+0x1C,4)!=0;
    if(!transition && now-state.last<100) return false;
    state.vehicle=vehicle;state.last=now;++state.count;memcpy(state.prior,bytes,0x30);return true;
}
static void CapturePlayerActions(void* component,void* payload) {
    if(!payload || !component || !inputVehicleID ||
       Read<void*>(component,0)!=reinterpret_cast<void*>(image+0x4C46F98) ||
       Read<unsigned>(component,0xC)!=inputVehicleID) return;
    auto reference=ResolveInputReference(inputVehicleID);
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto high=Read<void*>(Read<void*>(player,0x228),8);
    if(!reference || !PlayerDrivesTarget(high,ReferenceHandle(reference))) return;
    unsigned char bytes[0x30];memcpy(bytes,payload,sizeof(bytes));
    auto now=GetTickCount64();unsigned count=0;ULONGLONG elapsed=0;
    AcquireSRWLockExclusive(&actionCaptureLock);
    bool due=ActionCaptureDue(actionCapture,inputVehicleID,bytes,now);
    if(due) {count=actionCapture.count;elapsed=now-actionCapture.start;}
    ReleaseSRWLockExclusive(&actionCaptureLock);
    if(!due) return;
    char hex[97];for(unsigned i=0;i<sizeof(bytes);++i) snprintf(hex+i*2,3,"%02X",bytes[i]);
    char message[600];
    snprintf(message,sizeof(message),"PLAYER_ACTION_CAPTURE vehicle=%08X sample=%u elapsedMs=%llu raw=%s f00=%.6f f04=%.6f f08=%.6f f0C=%.6f steer=%.6f forward=%.6f reverse=%.6f b1C=%u b1D=%u b1E=%u b1F=%u f20=%.6f f24=%.6f f28=%.6f f2C=%.6f",
        inputVehicleID,count,elapsed,hex,Read<float>(bytes,0),Read<float>(bytes,4),Read<float>(bytes,8),Read<float>(bytes,12),Read<float>(bytes,16),Read<float>(bytes,20),Read<float>(bytes,24),bytes[28],bytes[29],bytes[30],bytes[31],Read<float>(bytes,32),Read<float>(bytes,36),Read<float>(bytes,40),Read<float>(bytes,44));
    Line(message);
    if(count==1 || bytes[0x1F]) CaptureWeaponRouting(component,inputVehicleID);
}

static void ObserveInputApply(void* component,void* payload) {
    CapturePlayerActions(component,payload);
    if(inputVehicleID && (SmoothLease(GetTickCount64()) || ConvoyLease(convoyActor.load(),GetTickCount64())) &&
       Read<void*>(component,0)==reinterpret_cast<void*>(image+0x4C46F98) && Read<unsigned>(component,0xC)==inputVehicleID) {
        // Same form lookup and AsReference2 slot used by 021A13D3..021A13E7.
        auto reference=ResolveInputReference(inputVehicleID);
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        RememberLeadVehicle(reference,Read<void*>(Read<void*>(player,0x228),8),GetTickCount64());
    }
    const bool log=DriverObservationActive() && InputLogDue(inputVehicleID,GetTickCount64());
    if(log) {
        auto link=Read<void*>(component,0x10);
        auto status=Read<void*>(link,0xE8);
        char message[450];
        snprintf(message,sizeof(message),"VEHICLE_INPUT vehicle=%08X component=%p entityWord=%08X gate579=%u link=%p status=%p input10=%.6f input14=%.6f input18=%.6f status18=%.6f status1C=%.6f",inputVehicleID,component,Read<unsigned>(component,0xC),Read<unsigned char>(component,0x579),link,status,Read<float>(payload,0x10),Read<float>(payload,0x14),Read<float>(payload,0x18),Read<float>(status,0x18),Read<float>(status,0x1C));
        Line(message);
    }
    if(log) ObserveDirectPhysicsID(inputVehicleID);
    alignas(16) unsigned char filtered[0x30]{};
    bool adjusted=FilterPlayerAccelerator(component,payload,filtered);
    if(!adjusted) {AcquireSRWLockExclusive(&rampLock);if(inputVehicleID==playerRamp.vehicle || !SmoothLease(GetTickCount64())) playerRamp={};ReleaseSRWLockExclusive(&rampLock);}
    if(adjusted) {
        auto now=GetTickCount64(),previous=lastAccelerationLog.load();
        if(now-previous>=1000 && lastAccelerationLog.compare_exchange_strong(previous,now)) {
            char line[200];snprintf(line,sizeof(line),"PLAYER_ACCELERATOR vehicle=%08X requested=%.3f filtered=%.3f seconds=%.0f gradualRelease=%d",inputVehicleID,Read<float>(payload,0x14),Read<float>(filtered,0x14),accelerationSeconds.load(),smoothRelease.load());Line(line);
        }
    }
    inputApplyOriginal(component,adjusted?filtered:payload);
}

using RuntimeUpdateFn=std::uintptr_t(*)(void*,void*);
static RuntimeUpdateFn runtimeUpdateOriginal;
static SRWLOCK runtimeLogLock=SRWLOCK_INIT;
static InputLogSlot runtimeLogs[8];
static bool DriverObservationActive() {
    AcquireSRWLockShared(&driverLock);
    const bool active=driverProbe.actor && GetTickCount64()<driverProbe.deadline;
    ReleaseSRWLockShared(&driverLock);
    return active;
}
static bool RuntimeLogDue(unsigned id,ULONGLONG now) {
    if(!id) return false;
    AcquireSRWLockExclusive(&runtimeLogLock);
    InputLogSlot* found=nullptr;
    for(auto& slot:runtimeLogs) {if(slot.id==id) {found=&slot;break;}if(!slot.id && !found) found=&slot;}
    bool due=found && found->count<40 && (!found->count || now-found->last>=1000);
    if(due) {found->id=id;found->last=now;++found->count;}
    ReleaseSRWLockExclusive(&runtimeLogLock);
    return due;
}
static std::uintptr_t ObserveRuntimeUpdate(void* component,void* context) {
    // The native routine resolves its entity to a reference and compares it
    // with context+10 before using the physical link. Read that context only
    // during the call. The runtime table and civil base restrict observations.
    void* reference=Read<void*>(context,0x10);
    const bool supported=Read<void*>(component,0)==reinterpret_cast<void*>(image+0x4C46F98) &&
       SupportedPilotVehicle(reference);
    if(Read<void*>(component,0)==reinterpret_cast<void*>(image+0x4C46F98) && (ConvoyLease(convoyActor.load(),GetTickCount64()) || SmoothLease(GetTickCount64()))) {
        auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
        RememberLeadVehicle(reference,Read<void*>(Read<void*>(player,0x228),8),GetTickCount64());
    }
    if(supported && DriverObservationActive() && RuntimeLogDue(Read<unsigned>(reference,0x28),GetTickCount64())) {
        auto link=Read<void*>(component,0x10);auto status=Read<void*>(link,0xE8);
        char message[350];snprintf(message,sizeof(message),"VEHICLE_RUNTIME vehicle=%08X component=%p entityWord=%08X gate579=%u link=%p status=%p status18=%.6f status1C=%.6f",Read<unsigned>(reference,0x28),component,Read<unsigned>(component,0xC),Read<unsigned char>(component,0x579),link,status,Read<float>(status,0x18),Read<float>(status,0x1C));Line(message);
    }
    return runtimeUpdateOriginal(component,context);
}

struct PhysicsQueryAPI {
    void* (*gameSystems)();
    void* (*enterRead)(void*);
    void* (*lookup)(void*,void*,const std::uint64_t*);
};
static PhysicsQueryAPI physicsQueryAPI{};
struct PhysicsSnapshot {unsigned id{},entity{};unsigned char gate{};std::uintptr_t table{};bool found{},identity{};bool link{},status{};float steering{},drive{};};
static std::uint64_t PhysicsComponentKey(unsigned id,unsigned short type) {
    return (static_cast<std::uint64_t>(type)<<48)|(static_cast<std::uint64_t>(id)<<16);
}
static PhysicsSnapshot QueryPhysicsSnapshot(unsigned id,unsigned short type) {
    PhysicsSnapshot snap{};snap.id=id;
    if(!id || !type || type==0xFFFF || !physicsQueryAPI.gameSystems || !physicsQueryAPI.enterRead || !physicsQueryAPI.lookup) return snap;
    auto systems=physicsQueryAPI.gameSystems();
    auto manager=Read<void*>(systems,0x8B0);auto owner=Read<void*>(manager,0x238);
    if(!manager || !owner) return snap;
    // Same retained read-view owner and thread-local scope used by 012C0550.
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+8));
    auto scope=physicsQueryAPI.enterRead(static_cast<unsigned char*>(owner)+0x10);
    if(scope) {
        const auto key=PhysicsComponentKey(id,type);
        alignas(8) unsigned char result[32]{};
        physicsQueryAPI.lookup(static_cast<unsigned char*>(manager)+0x268,result,&key);
        auto pool=Read<void*>(result,0x10);auto index=Read<std::uint64_t>(result,0x18);
        auto count=Read<unsigned>(pool,4);
        if(pool && index<0xFE0 && index<count) {
            auto offset=Read<unsigned short>(pool,0x12+static_cast<size_t>(index)*4);
            auto entry=static_cast<unsigned char*>(pool)+offset;
            if(Read<std::uint64_t>(entry,0x10)==key) {
                auto component=entry+0x20;
                snap.found=true;snap.entity=Read<unsigned>(component,0xC);snap.identity=snap.entity==id;
                snap.table=reinterpret_cast<std::uintptr_t>(Read<void*>(component,0));
                if(snap.identity) {
                    snap.gate=Read<unsigned char>(component,0x579);
                    auto link=Read<void*>(component,0x10);auto status=Read<void*>(link,0xE8);
                    snap.link=link!=nullptr;snap.status=status!=nullptr;
                    snap.steering=Read<float>(status,0x18);snap.drive=Read<float>(status,0x1C);
                }
            }
        }
        // Match the readonly query's scope exit. No component escapes this call.
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(scope))==0)
            InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+0x21C),4);
    }
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+8))==0) {
        auto destroy=Read<void*>(Read<void*>(owner,0),0);
        if(destroy) reinterpret_cast<void(*)(void*,unsigned)>(destroy)(owner,1);
    }
    return snap;
}
#include "driver-motion.inc"
static SRWLOCK directLogLock=SRWLOCK_INIT;
static InputLogSlot directLogs[8];
static void ObserveDirectPhysics(void* vehicle) {ObserveDirectPhysicsID(Read<unsigned>(vehicle,0x28));}
static void ObserveDirectPhysicsID(unsigned id) {
    if(!id) return;
    AcquireSRWLockExclusive(&directLogLock);
    InputLogSlot* found=nullptr;
    for(auto& slot:directLogs) {if(slot.id==id) {found=&slot;break;}if(!slot.id && !found) found=&slot;}
    auto now=GetTickCount64();bool due=found && found->count<35 && (!found->count || now-found->last>=1000);
    if(due) {found->id=id;found->last=now;++found->count;}
    ReleaseSRWLockExclusive(&directLogLock);
    if(!due) return;
    auto type=Read<unsigned short>(reinterpret_cast<void*>(image+0x61E2548),0);
    auto snap=QueryPhysicsSnapshot(id,type);
    char message[320];snprintf(message,sizeof(message),"VEHICLE_DIRECT vehicle=%08X type=%04X found=%d entity=%08X identity=%d table=%p gate579=%u link=%d status=%d steering=%.6f drive=%.6f",id,type,snap.found,snap.entity,snap.identity,reinterpret_cast<void*>(snap.table),snap.gate,snap.link,snap.status,snap.steering,snap.drive);Line(message);
}

#include "graph-entry-observers.inc"

static bool Matches(std::uintptr_t rva,const unsigned char* bytes,std::size_t length) {
    return memcmp(reinterpret_cast<void*>(image+rva),bytes,length)==0;
}
extern "C" __declspec(dllexport) bool SFSEPlugin_Load(const SFSEInterface* sfse) {
    if (!sfse || sfse->runtimeVersion!=kRuntime) return false;
    image=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    releaseReferenceOriginal=reinterpret_cast<void(*)(void*)>(image+0x384600);
    PWSTR documents=nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents))) return false;
    wchar_t path[1024];
    swprintf_s(path,L"%s\\My Games\\Starfield\\Logs",documents); CreateDirectoryW(path,nullptr);
    swprintf_s(path,L"%s\\My Games\\Starfield\\Logs\\SFSE",documents); CreateDirectoryW(path,nullptr);
    swprintf_s(path,L"%s\\My Games\\Starfield\\Logs\\SFSE\\RV8NativeTrace.log",documents);
    wchar_t backup[1040]; swprintf_s(backup,L"%s.previous",path); CopyFileW(path,backup,FALSE);
    output=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    CoTaskMemFree(documents);
    if(output==INVALID_HANDLE_VALUE) return false;
    wchar_t experimentPath[1024];
    GetModuleFileNameW(nullptr,experimentPath,1024);
    wchar_t* lastSlash=wcsrchr(experimentPath,L'\\');
    if(!lastSlash) {Line("REJECT game directory unavailable");return false;}
    *lastSlash=L'\0';
    wcscat_s(experimentPath,L"\\Data\\SFSE\\Plugins\\RV8PassengerExperiment.enabled");
    passengerExperiment=GetFileAttributesW(experimentPath)!=INVALID_FILE_ATTRIBUTES;
    Line(passengerExperiment ? "LOAD v65; parked follower orders; full geometry selection and animated exit experiment" : "LOAD v65; parked follower orders; trace only; passenger experiment disabled");
    auto tasks=static_cast<SFSETaskInterface*>(sfse->QueryInterface(kInterface_Task));
    if(passengerExperiment && (!tasks || tasks->interfaceVersion<1 || !tasks->AddTaskPermanent)) {
        Line("REJECT task interface unavailable; no hooks installed");return false;
    }
    const unsigned char pose2072580[]={0xc4,0xc1,0x7a,0x10,0x80,0x8c,0x00,0x00,0x00,0xc3};
    if(!Matches(0x2072580,pose2072580,sizeof(pose2072580))) {Line("REJECT reference pose layout differs; no hooks installed");return false;}
    const unsigned char pose2072590[]={0xc4,0xc1,0x7a,0x10,0x80,0x90,0x00,0x00,0x00,0xc3};
    if(!Matches(0x2072590,pose2072590,sizeof(pose2072590))) {Line("REJECT reference pose layout differs; no hooks installed");return false;}
    const unsigned char pose20725a0[]={0xc4,0xc1,0x7a,0x10,0x80,0x94,0x00,0x00,0x00,0xc3};
    if(!Matches(0x20725a0,pose20725a0,sizeof(pose20725a0))) {Line("REJECT reference pose layout differs; no hooks installed");return false;}
    const unsigned char pose2070320[]={0xc4,0xc1,0x7a,0x10,0x80,0x88,0x00,0x00,0x00};
    if(!Matches(0x2070320,pose2070320,sizeof(pose2070320))) {Line("REJECT reference pose layout differs; no hooks installed");return false;}
    const unsigned char savedSnapBytes[]={0x48,0x89,0x5C,0x24,0x10,0x55,0x56,0x57,0x41,0x54};
    if(!Matches(0x1799E50,savedSnapBytes,sizeof(savedSnapBytes))) {Line("REJECT Papyrus saved-seat wrapper signature differs; no hooks installed");return false;}
    const unsigned char interactionBytes[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57};
    if(!Matches(0x12B2E20,interactionBytes,sizeof(interactionBytes))) {Line("REJECT interaction observer signature differs; no hooks installed");return false;}
    const unsigned char outerBytes[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89};
    if(!Matches(0x18F69A0,outerBytes,sizeof(outerBytes))) {Line("REJECT outer snap signature differs; no hooks installed");return false;}
    const unsigned char selectBytes[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57};
    const unsigned char snapBytes[]={0x48,0x89,0x5C,0x24,0x10,0x44,0x88,0x4C,0x24,0x20,0x55,0x56,0x57};
    const unsigned char exitBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x44,0x88,0x48,0x20};
    const unsigned char stateBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x44,0x88,0x48,0x20};
    const unsigned char eventBytes[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x50};
    const unsigned char passengerBytes[]={0x48,0x8B,0xC4,0x44,0x89,0x48,0x20,0x44,0x88,0x40,0x18};
    const unsigned char prepareBytes[]={0x48,0x8B,0xC4,0x44,0x88,0x40,0x18,0x48,0x89,0x50,0x10};
    const unsigned char endBytes[]={0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08};
    const unsigned char markerBytes[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,0x89,0x73,0x18};
    const unsigned char getUpBytes[]={0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4C,0x24,0x08};
    if(!Matches(0xB9CD00,markerBytes,sizeof(markerBytes)) || !Matches(0x19FFFD0,getUpBytes,sizeof(getUpBytes))) {
        Line("REJECT selection/animated exit bytes differ; no hooks installed");return false;
    }
    if(!Matches(0x1A04C30,prepareBytes,sizeof(prepareBytes)) || !Matches(0x1A9CC40,endBytes,sizeof(endBytes))) {
        Line("REJECT passenger preparation/exit bytes differ; no hooks installed");return false;
    }
    if(!Matches(0x1315260,passengerBytes,sizeof(passengerBytes))) {
        Line("REJECT passenger entry bytes differ; no hooks installed"); return false;
    }
    passengerOriginal=reinterpret_cast<PassengerFn>(image+0x1315260);
    const unsigned char keywordBytes[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x83,0xC1,0x68};
    if(!Matches(0x47FCE0,keywordBytes,sizeof(keywordBytes))) {Line("REJECT keyword query differs; no hooks installed");return false;}
    hasKeywordOriginal=reinterpret_cast<HasKeywordFn>(image+0x47FCE0);
    if(!Matches(0xB9D650,selectBytes,sizeof(selectBytes)) || !Matches(0x18F6AD0,snapBytes,sizeof(snapBytes)) || !Matches(0x19942C0,exitBytes,sizeof(exitBytes)) || !Matches(0x1A03F70,stateBytes,sizeof(stateBytes)) || !Matches(0xB96A50,eventBytes,sizeof(eventBytes))) {
        Line("REJECT function bytes differ; no hooks installed"); return false;
    }
    const unsigned char gateObserverBytes[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48};
    if(!Matches(0x1AFBF30,gateObserverBytes,sizeof(gateObserverBytes))) {Line("REJECT gate observer signature differs; no hooks installed");return false;}
    const unsigned char entryObserverBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x44,0x88,0x40,0x18,0x55};
    if(!Matches(0x19FF3B0,entryObserverBytes,sizeof(entryObserverBytes))) {Line("REJECT entry observer signature differs; no hooks installed");return false;}
    const unsigned char candidatesBytes[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x08,0x49,0x89,0x6B,0x10};
    if(!Matches(0x636290,candidatesBytes,sizeof(candidatesBytes))) {Line("REJECT animation candidate signature differs; no hooks installed");return false;}
    const unsigned char transitionBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48,0x89,0x50,0x10,0x55,0x56,0x57};
    if(!Matches(0x12B37D0,transitionBytes,sizeof(transitionBytes))) {Line("REJECT vehicle transition signature differs; no hooks installed");return false;}
    const unsigned char animationObserverBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48,0x89,0x48,0x08,0x55};
    if(!Matches(0x19F7100,animationObserverBytes,sizeof(animationObserverBytes))) {Line("REJECT animation observer signature differs; no hooks installed");return false;}
    const unsigned char variablesObserverBytes[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x44,0x88};
    if(!Matches(0x19C47F0,variablesObserverBytes,sizeof(variablesObserverBytes))) {Line("REJECT variables observer signature differs; no hooks installed");return false;}
    const unsigned char selectionBytes[]={0x48,0x89,0x5C,0x24,0x10,0x44,0x88,0x4C,0x24,0x20,0x4C,0x89};
    const unsigned char addBytes[]={0x48,0x85,0xD2,0x0F,0x84,0x73,0x04,0x00,0x00,0x48,0x89,0x5C};
    const unsigned char removeBytes[]={0x48,0x85,0xD2,0x0F,0x84,0xC7,0x00,0x00,0x00,0x48,0x89,0x5C};
    if(!Matches(0x131C920,selectionBytes,sizeof(selectionBytes)) || !Matches(0xB42710,addBytes,sizeof(addBytes)) || !Matches(0xB42CB0,removeBytes,sizeof(removeBytes))) {Line("REJECT follower protection signatures differ; no hooks installed");return false;}
    addKeywordOriginal=reinterpret_cast<ChangeKeywordFn>(image+0xB42710);
    removeKeywordOriginal=reinterpret_cast<ChangeKeywordFn>(image+0xB42CB0);
    const unsigned char dispatchBytes[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89};
    const unsigned char applyBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x20,0x56};
    if(!Matches(0x12C0550,dispatchBytes,sizeof(dispatchBytes)) || !Matches(0x1324650,applyBytes,sizeof(applyBytes))) {Line("REJECT input observer signatures differ; no hooks installed");return false;}
    const unsigned char updateBytes[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x48};
    if(!Matches(0x131D2C0,updateBytes,sizeof(updateBytes))) {Line("REJECT runtime update observer signature differs; no hooks installed");return false;}
    const unsigned char systemsBytes[]={0x48,0x83,0xEC,0x28,0xBA,0xB8,0x00,0x00,0x00,0x65,0x48,0x8B};
    if(!Matches(0x23FF640,systemsBytes,sizeof(systemsBytes))) {Line("REJECT physical query signature differs; no hooks installed");return false;}
    const unsigned char readscopeBytes[]={0x40,0x53,0x57,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9};
    if(!Matches(0x2404230,readscopeBytes,sizeof(readscopeBytes))) {Line("REJECT physical query signature differs; no hooks installed");return false;}
    const unsigned char formLookupBytes[]={0x89,0x4C,0x24,0x08,0x53,0x48,0x83,0xEC,0x20,0x33,0xDB};
    if(!Matches(0x5DE3B0,formLookupBytes,sizeof(formLookupBytes))) {Line("REJECT live input form lookup signature differs");return false;}
    inputFormLookup=reinterpret_cast<LookupFormFn>(image+0x5DE3B0);
    const unsigned char lightsBytes[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18};
    const unsigned char editorBytes[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20};
    if(!Matches(0x1314F50,lightsBytes,sizeof(lightsBytes)) || !Matches(0x31B850,editorBytes,sizeof(editorBytes))) {Line("REJECT convoy lights/keyword signature differs");return false;}
    const unsigned char lookupBytes[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48};
    if(!Matches(0x24105D0,lookupBytes,sizeof(lookupBytes))) {Line("REJECT physical query signature differs; no hooks installed");return false;}
    const unsigned char writeScopeBytes[]={0x41,0x54,0x41,0x55,0x48,0x83,0xEC,0x48,0x44,0x8B,0xA9,0x3C};
    if(!Matches(0x2404330,writeScopeBytes,sizeof(writeScopeBytes))) {Line("REJECT physical write scope signature differs; no hooks installed");return false;}
    enterPhysicsWrite=reinterpret_cast<EnterPhysicsWriteFn>(image+0x2404330);
    physicsQueryAPI={reinterpret_cast<void*(*)()>(image+0x23FF640),reinterpret_cast<void*(*)(void*)>(image+0x2404230),reinterpret_cast<void*(*)(void*,void*,const std::uint64_t*)>(image+0x24105D0)};
    if(MH_Initialize()!=MH_OK) {Line("REJECT MinHook initialization failed");return false;}
    struct Entry {std::uintptr_t rva;void* replacement;void** original;};
    Entry entries[]={ {0x1799E50,reinterpret_cast<void*>(&ObservePapyrusSnap),reinterpret_cast<void**>(&papyrusSnapOriginal)}, {0x1AF8F10,reinterpret_cast<void*>(&ObservePilotVariableContext),reinterpret_cast<void**>(&variableContextOriginal)}, {0x12B37D0,reinterpret_cast<void*>(&ScopedDriverTransition),reinterpret_cast<void**>(&vehicleTransitionOriginal)}, {0x18EEA70,reinterpret_cast<void*>(&ObservePilotProvider),reinterpret_cast<void**>(&animationProviderOriginal)}, {0xEF9980,reinterpret_cast<void*>(&ObservePilotResources),reinterpret_cast<void**>(&animationResourcesOriginal)}, {0xB42710,reinterpret_cast<void*>(&ObserveFleetAddKeyword),reinterpret_cast<void**>(&addKeywordOriginal)}, {0xB42CB0,reinterpret_cast<void*>(&ObserveFleetRemoveKeyword),reinterpret_cast<void**>(&removeKeywordOriginal)}, {0x47FCE0,reinterpret_cast<void*>(&ObserveFleetKeyword),reinterpret_cast<void**>(&hasKeywordOriginal)}, {0x1314F50,reinterpret_cast<void*>(&ObserveHeadlights),reinterpret_cast<void**>(&headlightsOriginal)}, {0x636290,reinterpret_cast<void*>(&ObserveAnimationCandidates),reinterpret_cast<void**>(&animationCandidatesOriginal)}, {0x18F69A0,reinterpret_cast<void*>(&OuterSnap),reinterpret_cast<void**>(&outerSnapOriginal)}, {0x131D2C0,reinterpret_cast<void*>(&ObserveRuntimeUpdate),reinterpret_cast<void**>(&runtimeUpdateOriginal)}, {0x12C0550,reinterpret_cast<void*>(&ObserveInputDispatch),reinterpret_cast<void**>(&inputDispatchOriginal)}, {0x1324650,reinterpret_cast<void*>(&ObserveInputApply),reinterpret_cast<void**>(&inputApplyOriginal)}, {0x131C920,reinterpret_cast<void*>(&SelectFrontFollower),reinterpret_cast<void**>(&followerSelectionOriginal)}, {0x12B2E20,reinterpret_cast<void*>(&ObserveInteractionEnter),reinterpret_cast<void**>(&interactionEnterOriginal)}, {0xB9D650,reinterpret_cast<void*>(&Select),reinterpret_cast<void**>(&selectOriginal)}, {0x18F6AD0,reinterpret_cast<void*>(&Snap),reinterpret_cast<void**>(&snapOriginal)}, {0x19942C0,reinterpret_cast<void*>(&Exit),reinterpret_cast<void**>(&exitOriginal)}, {0x1A03F70,reinterpret_cast<void*>(&State),reinterpret_cast<void**>(&stateOriginal)}, {0xB96A50,reinterpret_cast<void*>(&ExitEvent),reinterpret_cast<void**>(&exitEventOriginal)} , {0x1AFBF30,reinterpret_cast<void*>(&ObserveEntryGate),reinterpret_cast<void**>(&entryGateOriginal)}, {0x19FF3B0,reinterpret_cast<void*>(&ObserveNormalEntry),reinterpret_cast<void**>(&normalEntryOriginal)}, {0x19F7100,reinterpret_cast<void*>(&ObserveEntryAnimation),reinterpret_cast<void**>(&entryAnimationOriginal)}, {0x19C47F0,reinterpret_cast<void*>(&ObserveEntryVariables),reinterpret_cast<void**>(&entryVariablesOriginal)}};
    for(auto& entry:entries) {
        auto status=MH_CreateHook(reinterpret_cast<void*>(image+entry.rva),entry.replacement,entry.original);
        if(status!=MH_OK) {Line("REJECT hook creation failed; hooks not enabled");MH_Uninitialize();return false;}
        MH_QueueEnableHook(reinterpret_cast<void*>(image+entry.rva));
    }
    if(passengerExperiment) {
        if(MH_CreateHook(reinterpret_cast<void*>(image+0x1A04C30),reinterpret_cast<void*>(&Prepare),reinterpret_cast<void**>(&prepareOriginal))!=MH_OK) {
            Line("REJECT preparation hook failed; hooks not enabled");MH_Uninitialize();return false;
        }
        MH_QueueEnableHook(reinterpret_cast<void*>(image+0x1A04C30));
        if(MH_CreateHook(reinterpret_cast<void*>(image+0xB9CD00),reinterpret_cast<void*>(&MarkerQuery),reinterpret_cast<void**>(&markerQueryOriginal))!=MH_OK) {
            Line("REJECT marker query hook failed; hooks not enabled");MH_Uninitialize();return false;
        }
        MH_QueueEnableHook(reinterpret_cast<void*>(image+0xB9CD00));
        if(MH_CreateHook(reinterpret_cast<void*>(image+0x19FFFD0),reinterpret_cast<void*>(&GetUp),reinterpret_cast<void**>(&getUpOriginal))!=MH_OK) {
            Line("REJECT staggered exit hook failed; hooks not enabled");MH_Uninitialize();return false;
        }
        MH_QueueEnableHook(reinterpret_cast<void*>(image+0x19FFFD0));
    }
    if(MH_ApplyQueued()!=MH_OK) {Line("REJECT enable failed");MH_DisableHook(MH_ALL_HOOKS);MH_Uninitialize();return false;}
    if(passengerExperiment) tasks->AddTaskPermanent(&exitWatch);
    Line(passengerExperiment ? "READY nineteen hooks; bounded native steering calibration and close-range follow; pilot uses vehicle interaction without outer immediate placement; independent runtime identity observer; per-vehicle input observers preserve behavior; bounded driver GetUp hold and independent exit caller log; temporary front selection exclusion; interaction enter observer; v11 rear entry route restored; front selection protects reserved rear passengers; four entry observers preserve arguments and returns; parked follower orders, explicit release and staggered exit" : "READY nine observation hooks; original behavior preserved");
    return true;
}
