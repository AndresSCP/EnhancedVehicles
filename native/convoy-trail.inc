// Bounded, process-local measured lead trajectory. No physics/navigation ABI.
struct ConvoyTrail {
    VehiclePose points[256]{};
    unsigned count{},cursor{};
    unsigned long long lead{},follower{};
    ULONGLONG seen{},sample{};
    VehiclePose observed{};
    bool attached{},lost{};
};
struct TrailChoice {
    VehiclePose target{};
    float remaining{},crossTrack{},corner{};
    unsigned count{},cursor{};
    bool valid{},waiting{},boostSafe{};
};
static bool FiniteTrailPose(const VehiclePose& p) {
    return p.valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.yaw);
}
static float TrailDistance(const VehiclePose& a,const VehiclePose& b) {return std::hypot(a.x-b.x,a.y-b.y);}
static void RecordTrail(ConvoyTrail& t,unsigned long long identity,const VehiclePose& p,ULONGLONG now,bool active) {
    if(!active || !identity || !now || !FiniteTrailPose(p)) {t={};return;}
    if(t.lead!=identity || !t.seen || now<t.seen || now-t.seen>3000 ||
       (FiniteTrailPose(t.observed) && (TrailDistance(t.observed,p)>20.0f+60.0f*float(now-t.seen)/1000.0f || std::fabs(p.z-t.observed.z)>15.0f+40.0f*float(now-t.seen)/1000.0f))) t={};
    t.lead=identity;t.seen=now;t.observed=p;
    if(t.count && now>=t.sample && now-t.sample<100) return;
    if(t.count && TrailDistance(t.points[t.count-1],p)<1.5f && std::fabs(t.points[t.count-1].z-p.z)<0.75f) return;
    if(t.count==256) {
        if(t.attached && t.cursor==0) t.lost=true;
        for(unsigned i=1;i<256;++i) t.points[i-1]=t.points[i];
        --t.count;if(t.cursor) --t.cursor;
    }
    t.points[t.count++]=p;t.sample=now;
}
static float TrailProjection(const VehiclePose& a,const VehiclePose& b,const VehiclePose& p) {
    float x=b.x-a.x,y=b.y-a.y,d=x*x+y*y;
    return d>0.0001f?std::fmax(0.0f,std::fmin(1.0f,((p.x-a.x)*x+(p.y-a.y)*y)/d)):0;
}
static VehiclePose TrailInterpolate(const VehiclePose& a,const VehiclePose& b,float fraction) {
    return {a.x+(b.x-a.x)*fraction,a.y+(b.y-a.y)*fraction,a.z+(b.z-a.z)*fraction,b.yaw,true};
}
static TrailChoice SelectTrail(ConvoyTrail& t,unsigned long long follower,const VehiclePose& p,ULONGLONG now) {
    TrailChoice out{};out.count=t.count;out.cursor=t.cursor;
    if(!follower || !FiniteTrailPose(p) || !t.seen || now<t.seen || now-t.seen>2000 || t.count<2 || t.lost) {out.waiting=true;return out;}
    if(t.follower!=follower) {t.follower=follower;t.attached=false;t.cursor=0;}
    if(!t.attached) {
        float best=30.0f;unsigned chosen=0;bool found=false;
        // Attach only to the initial prefix, never globally jump to a later crossing.
        for(unsigned i=0;i+1<t.count && i<12;++i) {
            auto q=TrailInterpolate(t.points[i],t.points[i+1],TrailProjection(t.points[i],t.points[i+1],p));
            float d=TrailDistance(q,p);
            if(std::fabs(q.z-p.z)<5 && d<best-0.01f) {best=d;chosen=i;found=true;}
        }
        if(!found) {out.waiting=true;return out;}
        t.cursor=chosen;t.attached=true;
    }
    unsigned c=t.cursor;
    // Progress is ordered and bounded per tick; nearby future branches are ignored.
    for(unsigned steps=0;c+2<t.count && steps<12;++steps) {
        float projection=TrailProjection(t.points[c],t.points[c+1],p);
        if(std::fabs(p.z-t.points[c+1].z)>3 ||
           (TrailDistance(p,t.points[c+1])>1.75f && !(projection>=0.99f && TrailDistance(p,t.points[c+1])<4))) break;
        ++c;
    }
    auto projected=TrailInterpolate(t.points[c],t.points[c+1],TrailProjection(t.points[c],t.points[c+1],p));
    out.crossTrack=TrailDistance(projected,p);
    if(out.crossTrack>15 || std::fabs(projected.z-p.z)>5) {out.waiting=true;return out;}
    t.cursor=c;out.cursor=c;
    float length=TrailDistance(projected,t.points[c+1]);
    for(unsigned i=c+1;i+1<t.count;++i) length+=TrailDistance(t.points[i],t.points[i+1]);
    if(!std::isfinite(length)) {out.waiting=true;return out;}
    out.remaining=std::fmax(0.0f,length-8.0f);
    float ahead=std::fmin(6.0f,out.remaining);auto from=projected;
    out.target=projected;
    for(unsigned i=c+1;i<t.count;++i) {
        auto to=t.points[i];float segment=TrailDistance(from,to);
        if(segment>=ahead && segment>0.0001f) {out.target=TrailInterpolate(from,to,ahead/segment);break;}
        ahead-=segment;out.target=to;
        if(i+1<t.count) {
            auto next=t.points[i+1];
            float turn=std::fabs(std::remainder(std::atan2(to.x-from.x,to.y-from.y)-std::atan2(next.x-to.x,next.y-to.y),6.28318530718f));
            out.corner=std::fmax(out.corner,turn);
            if(turn>0.6f && TrailDistance(p,to)>1.75f) break;
        }
        from=to;
    }
    float straightDistance=0;
    float previousHeading=std::atan2(t.points[c+1].x-projected.x,t.points[c+1].y-projected.y);
    auto previous=projected;bool straight=true;
    for(unsigned i=c+1;i<t.count && straightDistance<30.0f;++i) {
        float segment=TrailDistance(previous,t.points[i]);
        if(segment>0.01f) {
            float heading=std::atan2(t.points[i].x-previous.x,t.points[i].y-previous.y);
            if(std::fabs(std::remainder(heading-previousHeading,6.28318530718f))>0.12f) {straight=false;break;}
            previousHeading=heading;straightDistance+=segment;
        }
        previous=t.points[i];
    }
    out.boostSafe=straight && straightDistance>=30 && out.crossTrack<=1.0f;
    out.valid=FiniteTrailPose(out.target);return out;
}
static SRWLOCK convoyTrailLock=SRWLOCK_INIT;
static ConvoyTrail convoyTrail;
static void ClearConvoyTrail() {AcquireSRWLockExclusive(&convoyTrailLock);convoyTrail={};ReleaseSRWLockExclusive(&convoyTrailLock);}
static void SampleConvoyTrail(unsigned long long id,const VehiclePose& p,ULONGLONG now,bool active) {
    AcquireSRWLockExclusive(&convoyTrailLock);RecordTrail(convoyTrail,id,p,now,active);ReleaseSRWLockExclusive(&convoyTrailLock);
}
static TrailChoice ChooseConvoyTrail(unsigned long long follower,const VehiclePose& p,ULONGLONG now) {
    AcquireSRWLockExclusive(&convoyTrailLock);auto result=SelectTrail(convoyTrail,follower,p,now);ReleaseSRWLockExclusive(&convoyTrailLock);return result;
}
