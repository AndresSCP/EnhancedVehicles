// Observation only. Do not initiate, suppress, or replace entry operations.
using EntryGateFn=bool(*)(void*,void*,void*);
using NormalEntryFn=bool(*)(void*,void*,bool);
using EntryAnimationFn=bool(*)(void*,void*,void*,void*);
using EntryVariablesFn=void(*)(void*,void*,void*,bool);
static EntryGateFn entryGateOriginal;
static NormalEntryFn normalEntryOriginal;
static EntryAnimationFn entryAnimationOriginal;
static EntryVariablesFn entryVariablesOriginal;
static std::atomic<unsigned> entryObservations{0};
static void EntryObservation(const char* phase,void* actor,void* descriptor,int result) {
    if(!actor || !Interesting(actor)) return;
    void* high=Read<void*>(Read<void*>(actor,0x228),8);
    // Ignore generic graph queries while no furniture entry is active.
    if(!high || (!Read<unsigned>(high,0x424) && !Read<unsigned>(high,0x428))) return;
    if(entryObservations.fetch_add(1)>=400) return;
    char message[850];
    snprintf(message,sizeof(message),
        "ENTRY_OBSERVER phase=%s actor=%08X result=%d flagsFC=%08X desiredHandle=%08X currentHandle=%08X desiredSeat=%d currentSeat=%d animationKey=%016llX auxiliary=%p pendingVariableCount=%u previousVariableCount=%u descriptorCount=%u descriptorCapacityBits=%08X positionBC=%.3f,%.3f,%.3f positionC8=%.3f,%.3f,%.3f",
        phase,Read<std::uint32_t>(actor,0x28),result,Read<std::uint32_t>(actor,0xFC),
        Read<std::uint32_t>(high,0x424),Read<std::uint32_t>(high,0x428),Read<int>(high,0x4F8),Read<int>(high,0x4FC),
        static_cast<unsigned long long>(Read<std::uint64_t>(high,0x4F0)),Read<void*>(high,0x440),
        Read<unsigned>(high,0x350),Read<unsigned>(high,0x330),Read<unsigned>(descriptor,0),Read<unsigned>(descriptor,8),
        Read<float>(high,0xBC),Read<float>(high,0xC0),Read<float>(high,0xC4),
        Read<float>(high,0xC8),Read<float>(high,0xCC),Read<float>(high,0xD0));
    Line(message);
}
static std::atomic<unsigned> gateVariableIDs{0},preparedVariableIDs{0};
static void ObserveVariableIDs(void* actor,void* descriptor,const char* phase) {
    if(!actor || !Interesting(actor) || !descriptor) return;
    auto count=Read<unsigned>(descriptor,0);
    auto capacity=Read<int>(descriptor,8);
    if(!count || count>8 || (capacity>=0 && static_cast<unsigned>(capacity)<count) || (capacity<0 && count>2)) return;
    const void* entries=capacity<0 ? static_cast<unsigned char*>(descriptor)+0x10 : Read<void*>(descriptor,0x10);
    if(!entries) return;
    auto& counter=strcmp(phase,"prepared")==0 ? preparedVariableIDs : gateVariableIDs;
    if(counter.fetch_add(1)>=100) return;
    char message[320];size_t used=snprintf(message,sizeof(message),"ENTRY_VARIABLE_IDS phase=%s actor=%08X count=%u IDs=",phase,Read<unsigned>(actor,0x28),count);
    for(unsigned i=0;i<count;++i) used+=snprintf(message+used,sizeof(message)-used," %016llX",static_cast<unsigned long long>(Read<std::uint64_t>(entries,i*8)));
    Line(message);
}

static bool ObserveEntryGate(void* actor,void* descriptor,void* value) {
    bool result=entryGateOriginal(actor,descriptor,value);
    EntryObservation("graph-gate-return",actor,descriptor,result);
    auto high=Read<void*>(Read<void*>(actor,0x228),8);
    if(high && (Read<unsigned>(high,0x424) || Read<unsigned>(high,0x428))) ObserveVariableIDs(actor,descriptor,"gate");
    return result;
}
static bool ObserveNormalEntry(void* process,void* actor,bool flag) {
    EntryObservation("normal-entry-before",actor,nullptr,flag);
    bool result=normalEntryOriginal(process,actor,flag);
    EntryObservation("normal-entry-after",actor,nullptr,result);
    return result;
}
static bool ObserveEntryAnimation(void* process,void* actor,void* furniture,void* auxiliary) {
    EntryObservation("animation-before",actor,nullptr,-1);
    bool result=entryAnimationOriginal(process,actor,furniture,auxiliary);
    EntryObservation("animation-after",actor,nullptr,result);
    return result;
}
static void ObserveEntryVariables(void* process,void* actor,void* furniture,bool flag) {
    EntryObservation("variables-before",actor,nullptr,flag);
    entryVariablesOriginal(process,actor,furniture,flag);
    EntryObservation("variables-after",actor,nullptr,flag);
    auto high=Read<void*>(process,8);
    if(high) ObserveVariableIDs(actor,static_cast<unsigned char*>(high)+0x350,"prepared");
}
