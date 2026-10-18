// Experimental physical input bridge. The view, component and gate are local
// to one native call. No player handler/global vehicle is replaced.
// RCX: page allocator; RDX: pool header. Both are consumed at entry.
using EnterPhysicsWriteFn=void(*)(void*,void*);
static EnterPhysicsWriteFn enterPhysicsWrite=nullptr;
static std::atomic<unsigned> physicalWriteLogs{0};
static unsigned short (*driverLightTypeGetter)()=+[]()->unsigned short {return Read<unsigned short>(reinterpret_cast<void*>(image+0x61DD7F8),0);};
static bool BuildDriverPayload(float steering,float throttle,unsigned char* payload,float reverse=0,bool forwardBoost=false,unsigned actionTest=0) {
    if(!payload || !std::isfinite(steering) || !std::isfinite(throttle) ||
       steering < -1.0f || steering > 1.0f || throttle < 0.0f || throttle > 1.0f || !std::isfinite(reverse) || reverse<0 || reverse>0.30f || (reverse>0 && throttle>0)) return false;
    if(actionTest>4 || ((actionTest==1 || actionTest==2 || actionTest==4) && (steering!=0 || throttle!=0 || reverse!=0 || forwardBoost)) || (actionTest==3 && (reverse!=0 || forwardBoost))) return false;
    if(forwardBoost && (reverse!=0 || throttle<0.9f || std::fabs(steering)>0.2f)) return false;
    memset(payload,0,0x30);
    memcpy(payload+0x10,&steering,4);memcpy(payload+0x14,&throttle,4);memcpy(payload+0x18,&reverse,4);
    payload[0x1C]=forwardBoost?1:0; // v43 player-correlated forward boost; native resource checks remain authoritative.
    payload[0x1E]=(actionTest==2 || actionTest==4)?1:0;
    payload[0x1F]=(actionTest==1 || actionTest==3)?1:0;
    return true;
}
static bool ApplyDriverInput(unsigned id,unsigned short type,float steering,float throttle,int lightsState=-1,bool* lightsApplied=nullptr,float reverse=0,bool forwardBoost=false,unsigned actionTest=0) {
    alignas(16) unsigned char payload[0x30]{};
    if(!id || type!=0x77 || !image || !enterPhysicsWrite || !inputApplyOriginal ||
       !physicsQueryAPI.gameSystems || !physicsQueryAPI.enterRead || !physicsQueryAPI.lookup ||
       !BuildDriverPayload(steering,throttle,payload,reverse,forwardBoost,actionTest)) return false;
    auto manager=Read<void*>(physicsQueryAPI.gameSystems(),0x8B0);
    auto owner=Read<void*>(manager,0x238);
    if(!manager || !owner) return false;
    bool applied=false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+8));
    __try {
    auto scope=physicsQueryAPI.enterRead(static_cast<unsigned char*>(owner)+0x10);
    if(scope) {
        auto depthBefore=Read<LONG>(scope,4);
        __try {
            const auto key=PhysicsComponentKey(id,type);
            alignas(8) unsigned char result[32]{};
            physicsQueryAPI.lookup(static_cast<unsigned char*>(manager)+0x268,result,&key);
            auto pool=Read<unsigned char*>(result,0x10);auto index=Read<std::uint64_t>(result,0x18);
            if(pool && index<0xFE0 && index<Read<unsigned>(pool,4)) {
                auto entry=pool+Read<unsigned short>(pool,0x12+static_cast<size_t>(index)*4);
                auto component=entry+0x20;
                auto link=Read<void*>(component,0x10);auto status=Read<void*>(link,0xE8);
                auto poolOwner=Read<void*>(pool-0x20,8);
                if(Read<std::uint64_t>(entry,0x10)==key && Read<unsigned>(component,0xC)==id &&
                   Read<void*>(component,0)==reinterpret_cast<void*>(image+0x4C46F98) &&
                   link && status && poolOwner) {
                    // Same native write entry used by 012C0550 before InputApply.
                    bool logWrite=physicalWriteLogs.fetch_add(1)<4;
                    if(logWrite) {
                        char message[330];snprintf(message,sizeof(message),"DRIVER_WRITE_BEGIN vehicle=%08X allocator=%p header=%p component=%p allocatorEpoch=%u headerEpoch=%u throttle=%.3f",id,poolOwner,pool-0x20,component,Read<unsigned>(poolOwner,0x23C),Read<unsigned>(pool-0x20,0x10),throttle);Line(message);
                        if(output!=INVALID_HANDLE_VALUE) FlushFileBuffers(output);
                    }
                    enterPhysicsWrite(poolOwner,pool-0x20);
                    if(logWrite) Line("DRIVER_WRITE_ACQUIRED native page header accepted");
                    if(lightsState>=0 && headlightsOriginal) {
                        static std::atomic<unsigned> lightDiagnostics{0};
                        auto lightType=driverLightTypeGetter?driverLightTypeGetter():0;
                        auto lightKey=PhysicsComponentKey(id,lightType);
                        alignas(8) unsigned char lightResult[32]{};
                        physicsQueryAPI.lookup(static_cast<unsigned char*>(manager)+0x268,lightResult,&lightKey);
                        auto lightPool=Read<unsigned char*>(lightResult,0x10);auto lightIndex=Read<std::uint64_t>(lightResult,0x18);
                        if(lightDiagnostics.fetch_add(1)<8) {char diagnostic[180];snprintf(diagnostic,sizeof(diagnostic),"CONVOY_LIGHT_LOOKUP vehicle=%08X type=%u pool=%p index=%llu",id,lightType,lightPool,lightIndex);Line(diagnostic);}
                        if(lightType && lightPool && lightIndex<0xFE0 && lightIndex<Read<unsigned>(lightPool,4)) {
                            auto lightEntry=lightPool+Read<unsigned short>(lightPool,0x12+static_cast<size_t>(lightIndex)*4);
                            auto lightComponent=lightEntry+0x20;
                            if(Read<std::uint64_t>(lightEntry,0x10)==lightKey &&
                               Read<unsigned>(lightComponent,0)<128 && Read<void*>(lightPool-0x20,8)) {
                                // Same native component/effect-sequence arguments as 021A1640.
                                enterPhysicsWrite(Read<void*>(lightPool-0x20,8),lightPool-0x20);
                                AcquireSRWLockShared(&driverLock);auto ref=driverProbe.furniture;bool matching=Read<unsigned>(ref,0x28)==id;RetainReference(ref);ReleaseSRWLockShared(&driverLock);
                                __try {if(matching) {headlightsOriginal(lightsState!=0,lightComponent,ref,component+0x368,component+0x398);if(lightsApplied) *lightsApplied=true;Line(lightsState?"CONVOY_LIGHTS_APPLIED on":"CONVOY_LIGHTS_APPLIED off");}}
                                __finally {ReleaseReference(ref);}
                            }
                        }
                    }
                    auto previousGate=Read<unsigned char>(component,0x579);
                    __try {
                        component[0x579]=1;
                        inputApplyOriginal(component,payload);
                        if(actionTest) {
                            char message[240];snprintf(message,sizeof(message),"VEHICLE_ACTION_APPLY mode=%u vehicle=%08X fireLatch=%u verticalState=%u forwardState=%u charge=%.4f",actionTest,id,Read<unsigned char>(component,0x578),Read<unsigned char>(component,0x241),Read<unsigned char>(component,0x240),Read<float>(component,0x238));Line(message);
                        }
                        applied=true;
                    } __finally { component[0x579]=previousGate; }
                    if(logWrite) {
                        char message[250];snprintf(message,sizeof(message),"DRIVER_WRITE_END vehicle=%08X applied=%d restoredGate=%u steering=%.6f drive=%.6f",id,applied,Read<unsigned char>(component,0x579),Read<float>(status,0x18),Read<float>(status,0x1C));Line(message);
                    }
                }
            }
        } __finally {
            // Match the native write-view unwinding, including nested writes.
            if(Read<LONG>(scope,4)>depthBefore) {
                *reinterpret_cast<LONG*>(static_cast<unsigned char*>(scope)+4)=depthBefore;
                if(!depthBefore) {
                    LONG change=-(((Read<LONG>(owner,0x238)-1)&1)|2);
                    InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+0x218),change);
                }
            }
            if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(scope))==0)
                InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+0x21C),4);
        }
    }
    } __finally {
    if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(static_cast<unsigned char*>(owner)+8))==0) {
        auto destroy=Read<void*>(Read<void*>(owner,0),0);
        if(destroy) reinterpret_cast<void(*)(void*,unsigned)>(destroy)(owner,1);
    }
    }
    return applied;
}
static bool DriverMotionScope(const DriverProbeRecord& record,void* high,void* playerHigh,ULONGLONG now) {
    return record.motionArmed && !record.motionCancelled && DriverHoldScope(record,record.actor,high,now) &&
        SupportedPilotVehicle(record.furniture) &&
        Read<void*>(record.furniture,0xB0) && Read<void*>(record.actor,0xB0) &&
        (Read<unsigned>(record.actor,0xFC)&0x3800)==0x3000 &&
        !PlayerDrivesTarget(playerHigh,record.handle);
}
static float DriverPulseThrottle(const DriverProbeRecord& record,ULONGLONG now) {
    const auto began=record.started?record.started:record.deadline-30000;
    return now>=began+6000 && now<began+7000 ? 0.12f : 0.0f;
}
static void StopDriverRecord(const DriverProbeRecord& record) {
    if(!record.motionApplied) return;
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto playerHigh=Read<void*>(Read<void*>(player,0x228),8);
    if(!PlayerDrivesTarget(playerHigh,record.handle)) {
        auto type=Read<unsigned short>(reinterpret_cast<void*>(image+0x61E2548),0);
        bool cleared=ApplyDriverInput(Read<unsigned>(record.furniture,0x28),type,0.0f,0.0f);
        char line[150];snprintf(line,sizeof(line),"DRIVER_MOTION_NEUTRAL vehicle=%08X applied=%d",Read<unsigned>(record.furniture,0x28),cleared);Line(line);
    }
}
static void StopDriverMotion(void* actor) {
    AcquireSRWLockExclusive(&driverLock);
    auto record=driverProbe;
    bool matching=record.actor && record.actor==actor;
    if(matching) {
        driverProbe.motionCancelled=true;
        RetainReference(record.actor);RetainReference(record.furniture);
    }
    ReleaseSRWLockExclusive(&driverLock);
    if(matching) {StopDriverRecord(record);ReleaseReference(record.actor);ReleaseReference(record.furniture);}
}

static float WrapVehicleAngle(float radians) {
    return std::remainder(radians,6.28318530718f);
}
static VehiclePose ReadVehiclePose(void* reference) {
    VehiclePose p{};
    if(!reference || !Read<unsigned>(reference,0x28) || !Read<void*>(reference,0xB0)) return p;
    // Exact fields read by this runtime's GetPositionX/Y/Z and GetAngleZ.
    p.x=Read<float>(reference,0x8C);p.y=Read<float>(reference,0x90);
    p.z=Read<float>(reference,0x94);p.yaw=Read<float>(reference,0x88);
    p.valid=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::isfinite(p.yaw);
    return p;
}
static bool CalibrateVehicleDirection(const VehiclePose& before,const VehiclePose& after,float& yawSign,float& steeringSign) {
    if(!before.valid || !after.valid) return false;
    const float dx=after.x-before.x,dy=after.y-before.y;
    const float turn=WrapVehicleAngle(after.yaw-before.yaw);
    const float travel=std::hypot(dx,dy);
    if(!std::isfinite(turn) || travel<0.08f || travel>6.0f || std::fabs(turn)<0.002f || std::fabs(turn)>0.5f) return false;
    const float bearing=std::atan2(dx,dy);
    const float averageYaw=before.yaw+turn*0.5f;
    float same=std::fabs(WrapVehicleAngle(bearing-averageYaw));
    float inverted=std::fabs(WrapVehicleAngle(bearing+averageYaw));
    if(std::fmin(same,inverted)>0.25f || std::fabs(same-inverted)<0.02f) return false;
    yawSign=same<inverted?1.0f:-1.0f;
    steeringSign=turn*yawSign>0?1.0f:-1.0f;
    return true;
}
struct CalibrationEntry {unsigned id{},nativeHandle{},base{};float yaw{},steering{};};
static CalibrationEntry calibrationCache[8];
static SRWLOCK calibrationLock=SRWLOCK_INIT;
static CalibrationEntry CalibrationIdentity(void* vehicle,float yaw=0,float steering=0) {
    return {Read<unsigned>(vehicle,0x28),Read<unsigned>(vehicle,0x24),Read<unsigned>(Read<void*>(vehicle,0x98),0x28),yaw,steering};
}
static bool SameCalibrationVehicle(const CalibrationEntry& a,const CalibrationEntry& b) {
    return a.id && a.nativeHandle && a.base && a.id==b.id && a.nativeHandle==b.nativeHandle && a.base==b.base;
}
static bool RestoreDriverCalibration(DriverProbeRecord& record) {
    auto identity=CalibrationIdentity(record.furniture);bool restored=false;
    AcquireSRWLockShared(&calibrationLock);
    for(auto& saved:calibrationCache) if(SameCalibrationVehicle(identity,saved)) {
        record.forwardYawSign=saved.yaw;record.steeringSign=saved.steering;record.calibrated=true;restored=true;break;
    }
    ReleaseSRWLockShared(&calibrationLock);return restored;
}
static void SaveDriverCalibration(const DriverProbeRecord& record) {
    if(!record.calibrated || (record.forwardYawSign!=1 && record.forwardYawSign!=-1) || (record.steeringSign!=1 && record.steeringSign!=-1)) return;
    auto identity=CalibrationIdentity(record.furniture,record.forwardYawSign,record.steeringSign);
    if(!identity.id || !identity.nativeHandle || !identity.base) return;
    AcquireSRWLockExclusive(&calibrationLock);
    CalibrationEntry* slot=nullptr;
    for(auto& saved:calibrationCache) {if(SameCalibrationVehicle(identity,saved)) {slot=&saved;break;}if(!saved.id && !slot) slot=&saved;}
    if(!slot) slot=&calibrationCache[0];*slot=identity;
    ReleaseSRWLockExclusive(&calibrationLock);
}
enum class FollowReason { InvalidReference, CalibrationMissing, Close, Far, Height, Angle, Following };
static const char* FollowReasonName(FollowReason reason) {
    switch(reason) {
    case FollowReason::CalibrationMissing:return "calibration_missing";
    case FollowReason::Close:return "close_enough";
    case FollowReason::Far:return "target_too_far";
    case FollowReason::Height:return "height_difference";
    case FollowReason::Angle:return "outside_forward_angle";
    case FollowReason::Following:return "following";
    default:return "invalid_reference";
    }
}
struct FollowVehicleCommand {float steering{},throttle{},distance{},headingError{};bool allowed{};FollowReason reason{FollowReason::InvalidReference};};
static FollowVehicleCommand PlanVehicleFollow(const VehiclePose& vehicle,const VehiclePose& target,float yawSign,float steeringSign,float maxDistance=25.0f,float maxAngle=1.04719755f,float maxHeight=2.5f) {
    FollowVehicleCommand command{};
    if(!vehicle.valid || !target.valid) return command;
    if((yawSign!=1 && yawSign!=-1) || (steeringSign!=1 && steeringSign!=-1)) {command.reason=FollowReason::CalibrationMissing;return command;}
    const float dx=target.x-vehicle.x,dy=target.y-vehicle.y;
    command.distance=std::hypot(dx,dy);
    if(!std::isfinite(command.distance)) return command;
    if(command.distance<=5.0f) {command.reason=FollowReason::Close;return command;}
    if(command.distance>maxDistance) {command.reason=FollowReason::Far;return command;}
    if(!std::isfinite(target.z-vehicle.z) || !std::isfinite(maxHeight) || maxHeight<0 || std::fabs(target.z-vehicle.z)>maxHeight) {command.reason=FollowReason::Height;return command;}
    command.headingError=WrapVehicleAngle(std::atan2(dx,dy)-yawSign*vehicle.yaw);
    if(!std::isfinite(command.headingError)) return command;
    if(std::fabs(command.headingError)>maxAngle) {command.reason=FollowReason::Angle;return command;}
    command.allowed=true;command.reason=FollowReason::Following;
    command.steering=std::fmax(-0.5f,std::fmin(0.5f,command.headingError*2.0f))*steeringSign;
    command.throttle=0.12f*(1.0f-0.5f*std::fabs(command.headingError)/1.04719755f);
    // Approach gradually instead of halving input abruptly at seven units.
    command.throttle*=std::fmin(1.0f,(command.distance-5.0f)/5.0f);
    return command;
}
static FollowVehicleCommand PlanConvoyFollow(const VehiclePose& vehicle,const VehiclePose& target,float yawSign,float steeringSign) {
    // Allow a grade proportional to separation, with a small near-ground
    // tolerance. This is not a terrain query or navigation across floors.
    float horizontal=std::hypot(target.x-vehicle.x,target.y-vehicle.y);
    float heightAllowance=std::fmax(3.0f,horizontal*0.75f);
    auto command=PlanVehicleFollow(vehicle,target,yawSign,steeringSign,300.0f,3.14159265f,heightAllowance);
    if(command.allowed) {
        float separation=std::fmin(1.0f,(command.distance-5.0f)/25.0f);
        float angle=std::fabs(command.headingError);
        float corner=std::fmax(0.10f,1.0f-angle/1.57079633f);
        command.steering=std::fmax(-0.8f,std::fmin(0.8f,command.headingError*1.5f))*steeringSign;
        command.throttle=std::fmax(0.0f,std::fmin(1.0f,separation*corner));
        if(angle>0.78539816f) command.throttle=std::fmin(command.throttle,0.20f);
    }
    return command;
}
// Waypoint steering uses a short virtual extension only for heading calculation;
// throttle/distance use remaining ordered arc, never distance to the live lead.
static FollowVehicleCommand PlanTrailFollow(const VehiclePose& pose,const TrailChoice& route,float yawSign,float steeringSign) {
    FollowVehicleCommand command{};
    if(!route.valid || !FiniteTrailPose(pose) || !std::isfinite(route.remaining) || !std::isfinite(route.crossTrack)) return command;
    float demand=std::hypot(route.remaining,route.crossTrack);
    if(route.remaining<=1.0f && route.crossTrack<=2.0f) {command.reason=FollowReason::Close;return command;}
    auto aim=route.target;
    float dx=aim.x-pose.x,dy=aim.y-pose.y,d=std::hypot(dx,dy);
    if(d<0.01f) return command;
    if(d<8.0f) {aim.x=pose.x+dx*8.0f/d;aim.y=pose.y+dy*8.0f/d;}
    command=PlanConvoyFollow(pose,aim,yawSign,steeringSign);
    if(command.allowed) {
        command.distance=demand+5.0f;
        float corner=std::fmax(0.10f,1.0f-std::fabs(command.headingError)/1.57079633f);
        command.throttle=std::fmin(1.0f,demand/25.0f)*corner;
        if(std::fabs(command.headingError)>0.78539816f || route.corner>0.6f) command.throttle=std::fmin(command.throttle,0.20f);
    }
    return command;
}
// Position-derived measurements; never read guessed native velocity fields.
struct ConvoyDynamics {float speed{},yawRate{};bool valid{};};
static ConvoyDynamics MeasureConvoyDynamics(const VehiclePose& before,const VehiclePose& after,ULONGLONG beforeTime,ULONGLONG now,float yawSign) {
    ConvoyDynamics d{};
    if(!before.valid || !after.valid || !beforeTime || now<=beforeTime || now-beforeTime>500 || (yawSign!=1 && yawSign!=-1)) return d;
    float dt=float(now-beforeTime)/1000.0f;
    d.speed=std::hypot(after.x-before.x,after.y-before.y)/dt;
    d.yawRate=WrapVehicleAngle(after.yaw-before.yaw)*yawSign/dt;
    d.valid=std::isfinite(d.speed) && std::isfinite(d.yawRate) && d.speed<60 && std::fabs(d.yawRate)<12;
    return d;
}
static void StabilizeConvoyCommand(FollowVehicleCommand& command,const ConvoyDynamics& d,float steeringSign) {
    if(!command.allowed || !d.valid || (steeringSign!=1 && steeringSign!=-1)) return;
    float steer=command.steering*steeringSign-d.yawRate*0.22f;
    command.steering=std::fmax(-0.8f,std::fmin(0.8f,steer))*steeringSign;
    // Release the accelerator earlier when already approaching rapidly.
    float margin=command.distance-5.0f;
    if(d.speed>1 && margin<d.speed*0.55f) command.throttle=0;
}
static bool NeedsConvoyAssist(const VehiclePose& before,const VehiclePose& after,ULONGLONG beforeTime,ULONGLONG now,const FollowVehicleCommand& command,unsigned attempts) {
    if(!before.valid || !after.valid || !beforeTime || now<beforeTime || now-beforeTime<2500 || now-beforeTime>5000 || attempts>=3) return false;
    return command.allowed && command.distance>8 && command.throttle>0 &&
        std::hypot(after.x-before.x,after.y-before.y)<0.5f;
}
static bool CanReverseRecover(const VehiclePose& before,const VehiclePose& after,ULONGLONG beforeTime,ULONGLONG now,const FollowVehicleCommand& command,unsigned attempts,bool tried) {
    return !tried && attempts>=3 && command.allowed && command.distance>8 && command.throttle>0 &&
        before.valid && after.valid && beforeTime && now>=beforeTime && now-beforeTime>=2500 && now-beforeTime<=5000 &&
        std::hypot(after.x-before.x,after.y-before.y)<0.5f;
}
// Re-arm only after leaving the previous recovery area, never on a timer alone.
static bool CanRearmReverseRecovery(const VehiclePose& origin,const VehiclePose& pose,ULONGLONG previousUntil,ULONGLONG now,bool tried) {
    return tried && origin.valid && pose.valid && previousUntil && now>previousUntil &&
        now-previousUntil>=15000 && std::hypot(pose.x-origin.x,pose.y-origin.y)>=6.0f;
}
constexpr ULONGLONG kReverseRecoveryDurationMs=1600;
constexpr float kReverseRecoveryTravel=3.0f;
static bool ReverseRecoveryActive(const VehiclePose& origin,const VehiclePose& pose,ULONGLONG until,ULONGLONG now) {
    return until>now && until-now<=kReverseRecoveryDurationMs && origin.valid && pose.valid && std::hypot(pose.x-origin.x,pose.y-origin.y)<kReverseRecoveryTravel && std::fabs(pose.z-origin.z)<0.75f;
}
// Kinematic eligibility is not an obstacle/terrain visibility query.
// Only three short catch-up pulses per session; never jump or fire here.
static bool PlanForwardBoost(ForwardBoostPulse& state,const FollowVehicleCommand& follow,const ConvoyDynamics& dynamics,const VehiclePose& pose,const VehiclePose& target,ULONGLONG now,bool enabled,float reverse) {
    bool eligible=enabled && reverse==0 && follow.allowed && pose.valid && target.valid && dynamics.valid &&
        std::isfinite(follow.distance) && follow.distance>40 && follow.distance<150 &&
        std::isfinite(follow.headingError) && std::fabs(follow.headingError)<0.12f &&
        std::isfinite(follow.steering) && std::fabs(follow.steering)<0.2f && follow.throttle>=0.9f &&
        dynamics.speed>=2 && dynamics.speed<22 && std::fabs(dynamics.yawRate)<0.15f &&
        std::fabs(target.z-pose.z)<3;
    if(state.until) {
        bool active=eligible && now<state.until && state.until-now<=350 && state.origin.valid &&
            std::hypot(pose.x-state.origin.x,pose.y-state.origin.y)<8 && std::fabs(pose.z-state.origin.z)<1;
        if(!active) {state.until=0;state.cooldownUntil=now+8000;}
        return active;
    }
    if(!eligible || state.pulses>=3 || now<state.cooldownUntil) return false;
    state.until=now+350;state.origin=pose;++state.pulses;return true;
}
// Explicit diagnostic requests only; not target acquisition or automatic obstacle jumping.
static unsigned PlanVehicleActionTest(VehicleActionTest& state,const VehiclePose& pose,ULONGLONG now,bool playerWalking) {
    if(!state.mode) return 0;
    if(state.mode>2 || !pose.valid || !std::isfinite(pose.x) || !std::isfinite(pose.y) || !std::isfinite(pose.z) || !std::isfinite(pose.yaw) || !playerWalking || !state.requestedAt || now<state.requestedAt || now-state.requestedAt>3500) {state={};return 0;}
    if(state.stage==0) {state.stage=1;state.sample=pose;state.sampleTime=now;return 0;}
    if(state.stage==1) {
        if(std::hypot(pose.x-state.sample.x,pose.y-state.sample.y)>0.10f || std::fabs(pose.z-state.sample.z)>0.05f || std::fabs(WrapVehicleAngle(pose.yaw-state.sample.yaw))>0.04f) {state.sample=pose;state.sampleTime=now;return 0;}
        if(now<state.sampleTime || now-state.sampleTime<400) return 0;
        state.stage=2;state.origin=pose;state.until=now+(state.mode==1?200:150);
    }
    if(state.stage!=2 || now>=state.until || std::hypot(pose.x-state.origin.x,pose.y-state.origin.y)>1.0f || std::fabs(pose.z-state.origin.z)>0.6f) {state={};return 0;}
    return state.mode;
}
static bool CanStartVerticalRecovery(const DriverProbeRecord& record,const VehiclePose& pose,const FollowVehicleCommand& follow,const ConvoyDynamics& dynamics,ULONGLONG now) {
    return record.convoy && record.calibrated && record.verticalPolicy && record.verticalPolicyTime && now>=record.verticalPolicyTime && now-record.verticalPolicyTime<6000 &&
        !record.verticalUsed && record.reverseTried && record.reverseUntil && now>=record.reverseUntil && now-record.reverseUntil>=2500 &&
        record.assistAttempts>=3 && follow.allowed && follow.distance>12 && dynamics.valid && dynamics.speed<0.6f && std::fabs(dynamics.yawRate)<0.1f &&
        record.stallTime && now>=record.stallTime && now-record.stallTime>=2500 && now-record.stallTime<=5000 && record.stallPose.valid && pose.valid &&
        std::hypot(pose.x-record.stallPose.x,pose.y-record.stallPose.y)<0.35f && std::fabs(pose.z-record.stallPose.z)<0.05f;
}
static bool VerticalRecoveryActive(const DriverProbeRecord& record,const VehiclePose& pose,ULONGLONG now) {
    return record.verticalPolicy && record.verticalPolicyTime && now>=record.verticalPolicyTime && now-record.verticalPolicyTime<6000 &&
        record.verticalUntil>now && record.verticalUntil-now<=250 && record.verticalOrigin.valid && pose.valid &&
        std::hypot(pose.x-record.verticalOrigin.x,pose.y-record.verticalOrigin.y)<1.5f && std::fabs(pose.z-record.verticalOrigin.z)<1.0f;
}
static bool ForwardSupportFireActive(const DriverProbeRecord& record,const VehiclePose& pose,ULONGLONG now) {
    return record.fireUntil>now && record.fireUntil-now<=200 && record.fireOrigin.valid && pose.valid &&
        std::hypot(pose.x-record.fireOrigin.x,pose.y-record.fireOrigin.y)<2.0f &&
        std::fabs(WrapVehicleAngle(pose.yaw-record.fireOrigin.yaw))<0.10f && std::fabs(pose.z-record.fireOrigin.z)<0.25f;
}
static void TickDriverMotion() {
    auto now=GetTickCount64();
    AcquireSRWLockExclusive(&driverLock);
    if(driverProbe.actor && !driverProbe.motionArmed && !driverProbe.motionCancelled) {
        auto enteredHigh=Read<void*>(Read<void*>(driverProbe.actor,0x228),8);
        if(DriverHoldScope(driverProbe,driverProbe.actor,enteredHigh,now) && (Read<unsigned>(driverProbe.actor,0xFC)&0x3800)==0x3000 && HasOrderedSeatKeyword({driverProbe.actor,driverProbe.furniture,driverProbe.handle,0})) {
            driverProbe.motionArmed=true;Line("DRIVER_MOTION_ARMED delayed natural entry completed");
        }
    }
    auto record=driverProbe;
    bool due=record.actor && record.motionArmed && !record.motionCancelled &&
        (record.convoy || record.calibrated || now>=(record.started?record.started:record.deadline-30000)+6000) && (Read<unsigned>(record.actor,0xFC)&0x3800)==0x3000 && (!record.lastMotion || now-record.lastMotion>=100);
    if(due) {
        driverProbe.lastMotion=now;
        RetainReference(record.actor);RetainReference(record.furniture);
    }
    ReleaseSRWLockExclusive(&driverLock);
    if(!due) return;
    auto high=Read<void*>(Read<void*>(record.actor,0x228),8);
    auto player=Read<void*>(reinterpret_cast<void*>(image+0x5E61438),0);
    auto playerHigh=Read<void*>(Read<void*>(player,0x228),8);
    auto pose=ReadVehiclePose(record.furniture);
    bool eligible=DriverMotionScope(record,high,playerHigh,now) && pose.valid &&
        HasOrderedSeatKeyword({record.actor,record.furniture,record.handle,0});
    if(!eligible) {
        StopDriverMotion(record.actor);
        Line("DRIVER_MOTION_CANCEL scope lost or deadline reached");
    } else {
        if(record.convoy && !record.calibrated) {
            auto calibrationLead=SnapshotLead(now);
            bool ready=calibrationLead && calibrationLead!=record.furniture && PlayerDrivesTarget(playerHigh,ReferenceHandle(calibrationLead));
            ReleaseReference(calibrationLead);
            if(!ready) {
                record.started=now;record.calibrationAttempts=0;record.baseline={};
                AcquireSRWLockExclusive(&driverLock);
                if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {driverProbe.started=now;driverProbe.calibrationAttempts=0;driverProbe.baseline={};}
                ReleaseSRWLockExclusive(&driverLock);
            }
        }
        const auto elapsed=now-(record.started?record.started:record.deadline-30000);
        float steering=0,throttle=0,reverse=0;
        bool forwardBoost=false;ConvoyDynamics boostDynamics{};
        if(!record.baseline.valid) {
            record.baseline=pose;
            AcquireSRWLockExclusive(&driverLock);
            if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) driverProbe.baseline=pose;
            ReleaseSRWLockExclusive(&driverLock);
        }
        bool log=!record.lastFollowLog || now-record.lastFollowLog>=1000;
        const auto calibrationElapsed=elapsed-static_cast<ULONGLONG>(record.calibrationAttempts)*6000;
        if(!record.calibrated && record.calibrationAttempts<3 && calibrationElapsed>=6000 && calibrationElapsed<8000) {steering=0.45f;throttle=0.12f;}
        if(calibrationElapsed>=10000 && !record.calibrated && record.calibrationAttempts<3) {
            bool calibrated=CalibrateVehicleDirection(record.baseline,pose,record.forwardYawSign,record.steeringSign);
            record.calibrated=calibrated;
            AcquireSRWLockExclusive(&driverLock);
            if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {
                driverProbe.calibrated=calibrated;driverProbe.forwardYawSign=record.forwardYawSign;driverProbe.steeringSign=record.steeringSign;
                if(!calibrated) {driverProbe.calibrationAttempts++;driverProbe.baseline={};}
            }
            ReleaseSRWLockExclusive(&driverLock);
            char message[260];snprintf(message,sizeof(message),"DRIVER_CALIBRATION vehicle=%08X valid=%d yawSign=%.0f steeringSign=%.0f travel=%.4f yawDelta=%.6f",Read<unsigned>(record.furniture,0x28),calibrated,record.forwardYawSign,record.steeringSign,std::hypot(pose.x-record.baseline.x,pose.y-record.baseline.y),WrapVehicleAngle(pose.yaw-record.baseline.yaw));Line(message);
            if(calibrated) SaveDriverCalibration(record);
            else if(record.calibrationAttempts<2) Line("DRIVER_CALIBRATION retry scheduled");
            else Line("DRIVER_CALIBRATION no valid direction after three attempts; motion stays neutral");
        }
        FollowVehicleCommand follow{};
        void* lead=record.convoy?SnapshotLead(now):nullptr;
        auto target=ReadVehiclePose(player);
        bool leadValid=!record.convoy;
        if(lead) {
            leadValid=lead!=record.furniture && PlayerDrivesTarget(playerHigh,ReferenceHandle(lead));
            target=ReadVehiclePose(lead);
            if(target.valid) {target.x-=std::sin(record.forwardYawSign*target.yaw)*6.0f;target.y-=std::cos(record.forwardYawSign*target.yaw)*6.0f;}
        }
        if(record.convoy && !leadValid) {steering=0;throttle=0;}
        if((record.convoy || elapsed>=10000 && elapsed<24000) && record.calibrated && leadValid) {
            follow=record.convoy?PlanConvoyFollow(pose,target,record.forwardYawSign,record.steeringSign):PlanVehicleFollow(pose,target,record.forwardYawSign,record.steeringSign);
            if(record.convoy) {
                auto dynamics=MeasureConvoyDynamics(record.motionPose,pose,record.motionPoseTime,now,record.forwardYawSign);
                boostDynamics=dynamics;
                StabilizeConvoyCommand(follow,dynamics,record.steeringSign);
                if(CanRearmReverseRecovery(record.reverseOrigin,pose,record.reverseUntil,now,record.reverseTried)) {
                    bool rearmed=false;
                    AcquireSRWLockExclusive(&driverLock);
                    if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {
                        driverProbe.reverseTried=false;driverProbe.assistAttempts=0;driverProbe.verticalUsed=false;
                        record.reverseTried=false;record.assistAttempts=0;rearmed=true;
                    }
                    ReleaseSRWLockExclusive(&driverLock);
                    if(rearmed) Line("CONVOY_REVERSE_REARM previous recovery cleared; moved at least six units and cooldown elapsed");
                }
                if(!record.stallTime || now-record.stallTime>=2500) {
                    bool assist=NeedsConvoyAssist(record.stallPose,pose,record.stallTime,now,follow,record.assistAttempts);
                    bool recover=CanReverseRecover(record.stallPose,pose,record.stallTime,now,follow,record.assistAttempts,record.reverseTried);
                    bool jump=CanStartVerticalRecovery(record,pose,follow,dynamics,now);
                    bool moved=record.stallPose.valid && std::hypot(pose.x-record.stallPose.x,pose.y-record.stallPose.y)>1;
                    AcquireSRWLockExclusive(&driverLock);
                    if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {
                        driverProbe.stallPose=pose;driverProbe.stallTime=now;
                        if(jump) {
                            driverProbe.verticalUsed=true;driverProbe.verticalUntil=now+250;driverProbe.verticalOrigin=pose;
                            record.verticalUsed=true;record.verticalUntil=driverProbe.verticalUntil;record.verticalOrigin=pose;
                        }
                        if(moved) driverProbe.assistAttempts=0;
                        if(assist) {driverProbe.assistUntil=now+900;driverProbe.assistAttempts++;record.assistUntil=driverProbe.assistUntil;}
                        if(recover) {driverProbe.reverseTried=true;driverProbe.reverseUntil=now+kReverseRecoveryDurationMs;driverProbe.reverseOrigin=pose;record.reverseUntil=driverProbe.reverseUntil;record.reverseOrigin=pose;}
                    }
                    ReleaseSRWLockExclusive(&driverLock);
                    if(jump) Line("CONVOY_VERTICAL_RECOVERY one bounded pulse after forward and reverse attempts failed");
                    if(assist) Line("CONVOY_STALL_ASSIST brief forward input; maximum three attempts while stationary");
                    if(recover) Line("CONVOY_REVERSE_RECOVERY one bounded reverse attempt per recovery area");
                }
                if(record.assistUntil>now && follow.allowed && follow.distance>8) follow.throttle=std::fmax(follow.throttle,0.28f);
                AcquireSRWLockExclusive(&driverLock);
                if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {driverProbe.motionPose=pose;driverProbe.motionPoseTime=now;}
                ReleaseSRWLockExclusive(&driverLock);
            }
            steering=follow.steering;throttle=follow.throttle;
            if(record.convoy && follow.allowed && follow.distance>8 && ReverseRecoveryActive(record.reverseOrigin,pose,record.reverseUntil,now)) {throttle=0;reverse=0.22f;steering=-follow.steering;}
        }
        if(!record.convoy && std::hypot(pose.x-record.baseline.x,pose.y-record.baseline.y)>12.0f) {
            steering=0;throttle=0;StopDriverMotion(record.actor);
            Line("DRIVER_MOTION_STOP reason=travel_limit_12_units");
        }
        auto nextBoost=record.boost;
        forwardBoost=PlanForwardBoost(nextBoost,follow,boostDynamics,pose,target,now,passengerExperiment && record.convoy && record.calibrated && leadValid && now>=record.assistUntil && now>=record.reverseUntil,reverse);
        if(nextBoost.pulses!=record.boost.pulses || (record.boost.until && !nextBoost.until)) {
            char line[210];snprintf(line,sizeof(line),"CONVOY_FORWARD_BOOST phase=%s pulse=%u distance=%.3f speed=%.3f heading=%.3f",forwardBoost?"request":"release",nextBoost.pulses,follow.distance,boostDynamics.speed,follow.headingError);Line(line);
        }
        AcquireSRWLockExclusive(&driverLock);
        if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) driverProbe.boost=nextBoost;
        ReleaseSRWLockExclusive(&driverLock);
        auto type=Read<unsigned short>(reinterpret_cast<void*>(image+0x61E2548),0);
        int lightRequest=-1;
        int requested=leadLights.load();
        if(record.convoy && lead && convoyLights.load() && now-lightsHeartbeat.load()<6000 &&
           leadLightsVehicle.load()==Read<unsigned>(lead,0x28) && requested>=0 && requested!=record.appliedLights) lightRequest=requested;
        unsigned action=0;
        if(record.convoy && !record.actionTest.mode) {
            if(leadValid && VerticalRecoveryActive(record,pose,now)) {action=4;steering=0;throttle=0;reverse=0;forwardBoost=false;}
            else if(reverse==0 && !forwardBoost && ForwardSupportFireActive(record,pose,now)) action=3;
        }
        if(record.actionTest.mode) {
            auto nextTest=record.actionTest;
            action=PlanVehicleActionTest(nextTest,pose,now,Read<unsigned>(playerHigh,0x428)==0);
            // Settle, pulse and release without simultaneous driving or other actions.
            steering=0;throttle=0;reverse=0;forwardBoost=false;
            if(record.actionTest.stage!=nextTest.stage || record.actionTest.mode!=nextTest.mode) {
                char message[220];snprintf(message,sizeof(message),"VEHICLE_ACTION_TEST phase=%u mode=%u x=%.4f y=%.4f z=%.4f",nextTest.stage,record.actionTest.mode,pose.x,pose.y,pose.z);Line(message);
            }
            AcquireSRWLockExclusive(&driverLock);
            if(driverProbe.actor==record.actor && driverProbe.handle==record.handle && driverProbe.actionTest.requestedAt==record.actionTest.requestedAt) driverProbe.actionTest=nextTest;
            ReleaseSRWLockExclusive(&driverLock);
        }
        if(record.flightTest.requested) {
            auto flight=record.flightTest;
            bool eligible=Read<unsigned>(playerHigh,0x428)==0 && !record.actionTest.mode;
            action=PlanBoostFlightTest(flight,pose,now,eligible);
            steering=0;throttle=0;reverse=0;forwardBoost=false;
            if(log || flight.stage!=record.flightTest.stage || flight.pulses!=record.flightTest.pulses) {
                char line[300];snprintf(line,sizeof(line),"BOOST_FLIGHT_TEST stage=%u reason=%u height=%.3f vz=%.3f pulse=%u burnMs=%llu vertical=%u active=%d",flight.stage,flight.reason,flight.height,flight.verticalSpeed,flight.pulses,flight.burn,action==2,flight.requested!=0);Line(line);
            }
            AcquireSRWLockExclusive(&driverLock);
            if(driverProbe.actor==record.actor && driverProbe.handle==record.handle && driverProbe.flightTest.requested==record.flightTest.requested) driverProbe.flightTest=flight;
            ReleaseSRWLockExclusive(&driverLock);
        }
        bool lightsApplied=false;
        bool applied=ApplyDriverInput(Read<unsigned>(record.furniture,0x28),type,steering,throttle,lightRequest,&lightsApplied,reverse,forwardBoost,action);
        ReleaseReference(lead);
        if(applied) {
            AcquireSRWLockExclusive(&driverLock);
            if(driverProbe.actor==record.actor && driverProbe.handle==record.handle) {
                driverProbe.motionApplied=true;if(lightsApplied) driverProbe.appliedLights=lightRequest;if(log) driverProbe.lastFollowLog=now;
            }
            ReleaseSRWLockExclusive(&driverLock);
        }
        if(log || !applied) {
            char poseLine[280];snprintf(poseLine,sizeof(poseLine),"DRIVER_POSE_SAMPLE actor=%08X seat=%d flags=%08X key=%016llX auxiliary=%p",Read<unsigned>(record.actor,0x28),Read<int>(high,0x4FC),Read<unsigned>(record.actor,0xFC),static_cast<unsigned long long>(Read<std::uint64_t>(high,0x4F0)),Read<void*>(high,0x440));Line(poseLine);
            char line[450];snprintf(line,sizeof(line),"DRIVER_FOLLOW actor=%08X vehicle=%08X elapsedMs=%llu applied=%d x=%.4f y=%.4f z=%.4f targetZ=%.4f deltaZ=%.4f yaw=%.6f distance=%.4f error=%.6f steer=%.3f throttle=%.3f reverse=%.3f allowed=%d calibrated=%d reason=%s",Read<unsigned>(record.actor,0x28),Read<unsigned>(record.furniture,0x28),elapsed,applied,pose.x,pose.y,pose.z,target.z,target.z-pose.z,pose.yaw,follow.distance,follow.headingError,steering,throttle,reverse,follow.allowed,record.calibrated,elapsed<8000?"calibration_turn":elapsed<10000?"calibration_neutral":!record.convoy && elapsed>=24000?"time_limit_neutral":FollowReasonName(follow.reason));Line(line);
        }
        if(!applied) StopDriverMotion(record.actor);
    }
    ReleaseReference(record.actor);ReleaseReference(record.furniture);
}
