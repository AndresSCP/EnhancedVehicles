static void TestBoostFlightController() {
    VehiclePose p{0,0,10,0,true};
    BoostFlightTest s{};s.requested=1000;
    assert(PlanBoostFlightTest(s,p,1000,true)==0);
    for(ULONGLONG t=1100;t<1600;t+=100) assert(PlanBoostFlightTest(s,p,t,true)==0);
    assert(PlanBoostFlightTest(s,p,1600,true)==2 && s.stage==2 && s.pulses==1);
    BoostFlightTest sustained=s;
    assert(PlanBoostFlightTest(sustained,p,1800,true)==2);
    assert(PlanBoostFlightTest(sustained,p,2000,true)==2 && sustained.pulses==1);
    assert(PlanBoostFlightTest(sustained,p,2200,true)==2);
    assert(PlanBoostFlightTest(sustained,p,2300,true)==0);
    assert(PlanBoostFlightTest(sustained,p,2400,true)==2 && sustained.pulses==2);
    // Fast ascent releases thrust before reaching target height.
    p.z+=0.4f;assert(PlanBoostFlightTest(s,p,1700,true)==0 && s.verticalSpeed>3.9f);
    p.z=16.1f;assert(PlanBoostFlightTest(s,p,1800,true)==0 && !s.requested);
    s={};s.requested=2000;p={0,0,10,0,true};PlanBoostFlightTest(s,p,2000,true);
    for(ULONGLONG t=2100;t<=21900;t+=100) {
        auto action=PlanBoostFlightTest(s,p,t,true);
        assert(action==0 || action==2);
        if(t==11000) assert(action==2 && s.burn>4000);
        if(t>=16000) assert(action==0 && s.stage==3);
        assert(s.burn<=10000 && s.pulses<=25);
    }
    assert(PlanBoostFlightTest(s,p,22000,true)==0 && !s.requested && s.reason==2);
    s={};s.requested=1000;PlanBoostFlightTest(s,p,1000,true);
    assert(PlanBoostFlightTest(s,p,1700,true)==0 && s.reason==3 && !s.requested);
    s={};s.requested=1000;assert(PlanBoostFlightTest(s,p,1000,false)==0 && !s.requested);
    s={};s.requested=1000;p.z=std::nanf("");assert(PlanBoostFlightTest(s,p,1000,true)==0 && !s.requested);
    s={};s.requested=1000;p={0,0,10,0,true};PlanBoostFlightTest(s,p,1000,true);
    for(ULONGLONG t=1100;t<=1600;t+=100) PlanBoostFlightTest(s,p,t,true);
    p.x=4.1f;assert(PlanBoostFlightTest(s,p,1700,true)==0 && !s.requested);
    puts("PASS: boost flight waits for settled motion, uses vertical-only bounded pulses, anticipates ascent, releases for descent, caps burn/time and rejects stale/nonfinite/drifting/ineligible samples.");
}
