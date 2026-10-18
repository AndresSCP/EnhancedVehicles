// Relative-height experiment: no terrain clearance claim and no forced physics.
struct BoostFlightTest {
    ULONGLONG requested{},sampleTime{},settledSince{},pulseUntil{},cooldownUntil{},burn{};
    VehiclePose sample{},origin{};
    unsigned stage{},pulses{},reason{};
    float verticalSpeed{},height{};
};
static unsigned PlanBoostFlightTest(BoostFlightTest& s,const VehiclePose& p,ULONGLONG now,bool eligible) {
    if(!s.requested) return 0;
    auto stop=[&](unsigned reason)->unsigned {s.requested=0;s.stage=4;s.reason=reason;s.pulseUntil=0;return 0;};
    if(!eligible || !p.valid || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(p.yaw) || now<s.requested) return stop(1);
    auto elapsed=now-s.requested;
    if(elapsed>=20000) return stop(2);
    if(!s.sampleTime) {s.sample=p;s.sampleTime=now;s.settledSince=now;s.stage=1;return 0;}
    if(now<=s.sampleTime) return 0;
    auto dt=now-s.sampleTime;
    if(dt>500) return stop(3);
    s.verticalSpeed=(p.z-s.sample.z)*1000.0f/float(dt);
    float horizontal=std::hypot(p.x-s.sample.x,p.y-s.sample.y)*1000.0f/float(dt);
    s.sample=p;s.sampleTime=now;
    if(!std::isfinite(s.verticalSpeed) || std::fabs(s.verticalSpeed)>15 || horizontal>15) return stop(4);
    if(s.stage==1) {
        if(horizontal>0.25f || std::fabs(s.verticalSpeed)>0.15f) s.settledSince=now;
        if(elapsed>4000) return stop(5);
        if(now-s.settledSince<600) return 0;
        s.origin=p;s.stage=2;
    }
    s.height=p.z-s.origin.z;
    if(std::hypot(p.x-s.origin.x,p.y-s.origin.y)>4 || s.height>6 || s.height< -2) return stop(6);
    if(elapsed>=14000) {s.stage=3;s.pulseUntil=0;return 0;}
    float predicted=s.height+s.verticalSpeed*0.5f;
    if(s.pulseUntil) {
        if(now<s.pulseUntil && predicted<3.2f && s.verticalSpeed<3 && s.burn+dt<=10000) {s.burn+=dt;return 2;}
        s.pulseUntil=0;s.cooldownUntil=now+100;
    }
    if(now>=s.cooldownUntil && predicted<2.4f && s.verticalSpeed<2 && s.burn+dt<=10000 && s.pulses<25) {
        s.pulseUntil=now+650;++s.pulses;s.burn+=dt;return 2;
    }
    return 0;
}
