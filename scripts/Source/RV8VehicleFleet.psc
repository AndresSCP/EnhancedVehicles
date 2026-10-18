Scriptname RV8VehicleFleet extends Quest

RV8VehicleSettings Property Settings Auto
Weapon Property ControlGadget Auto
Message Property ActionMenu Auto
Form Property AdditionalVehicleBase Auto
FormList Property VehicleCatalogue Auto

ObjectReference[] ExtraVehicles
ObjectReference SelectedVehicle
bool RecoveryStopRequested = false
bool LoadRecoveryPending = false
int LoadRecoveryAttempts = 0
bool MenuBusy = false
bool ConvoySession = false
bool ConvoyStarting = false
bool FlightTestPreparing = false
bool ConvoyOrderRequested = false
bool AutoConvoySuppressed = false
Actor ConvoyOrderDriver
ObjectReference ConvoyOrderVehicle
float LastConvoyResumeAttempt = 0.0
bool ConvoyPassengerFillPending = false
bool DriverTransferPending = false
float DriverTransferStart = 0.0
Actor AssignedDriver
Actor ReservedDriver
bool AddedDriverExclusion = false
Actor[] ConvoyPassengers
bool LastPlayerDriving = false
float LastConvoyDiagnostic = 0.0
ObjectReference LastPlayerVehicle
Keyword[] AccelerationKeywords
Keyword GradualAccelerationKeyword
Keyword ThrottleReleaseKeyword
bool OwnAccelerationTag = false
bool OwnReleaseTag = false
int OwnedAccelerationTime = -1
Keyword ConvoyKeyword
Keyword SyncLightsKeyword
Keyword DriverExclusionKeyword
bool AddedConvoyKeyword = false
bool AddedLightsKeyword = false
bool FleetBusy = false
Actor SelectedFollower
Actor[] OrderedActors
ObjectReference[] OrderedVehicles
Keyword[] OrderedKeywords
bool[] AddedOrderKeywords
bool[] ReleaseRequested
float[] OrderStartTimes
Actor ProbeDriver
ObjectReference ProbeVehicle
Keyword ProbeDriverKeyword
bool ProbeAddedKeyword = false
Actor DriverPoseActor
int DriverPosePrevious = 0
bool DriverPoseOwned = false
float DriverPoseLastLog = 0.0
bool DriverPoseExitPending = false
int ProbeSamples = 0
float ProbeStartRealTime = 0.0

ObjectReference[] NamedExtraVehicles
int NextExtraVehicleNumber = 1

Keyword RobotKeyword
bool BoardingGroupBusy = false
Keyword CachedPrimarySeat
Keyword CachedFirstRearSeat
Keyword CachedSecondRearSeat

Form Function GetModForm(int localID) Global
    if Game.IsPluginInstalled("EnhancedVehicles.esm")
        return Game.GetFormFromFile(localID, "EnhancedVehicles.esm")
    elseif Game.IsPluginInstalled("EnhancedVehicles.esp")
        return Game.GetFormFromFile(localID, "EnhancedVehicles.esp")
    endif
    return None
EndFunction

Function Initialize() Global
    RV8VehicleFleet fleet = RV8VehicleFleet.GetModForm(0x809) as RV8VehicleFleet
    if fleet
        if !fleet.IsRunning()
            fleet.Start()
        endif
        fleet.InitializeControl()
        Debug.Notification("Vehicle Controller is ready.")
    else
        Debug.MessageBox("Vehicle Controller could not be initialized.")
    endif
EndFunction

Function WriteLog(string text)
    Debug.TraceUser("RV8VehicleFleet", "[RV8Fleet] " + text)
EndFunction

Event OnQuestInit()
    InitializeControl()
EndEvent

Function InitializeControl()
    Debug.OpenUserLog("RV8VehicleFleet")
    FlightTestPreparing = false
    VehicleActionProbeBusy = false
    RestoreCopilotHandPose()
    CopilotWeaponProbeBusy = false
    if !LoadRecoveryPending
        RestoreDriverPose()
    endif
    WriteLog("INITIALIZE Enhanced Vehicles consolidated beta; fleet v63; convoy boarding waits for player driving; retired name references pruned")
    if !ExtraVehicles
        ExtraVehicles = new ObjectReference[0]
    endif
    RegisterForRemoteEvent(Game.GetPlayer(), "OnItemEquipped")
    RegisterForRemoteEvent(Game.GetPlayer(), "OnPlayerLoadGame")
    if Settings
        Settings.InitializeSettings()
    endif
    EnsureGadget()
    RememberActiveModel()
    EnsureOrderStorage()
    RefreshPassengerForms()
    EnsureExtraVehicleNames()
    BoardingGroupBusy = false
    if !LoadRecoveryPending
        if ConvoySession
            EndConvoy(false)
        elseif ProbeDriver
            StopDriverOrder()
        endif
    endif
    ConvoyKeyword = RV8VehicleFleet.GetModForm(0x80B) as Keyword
    SyncLightsKeyword = RV8VehicleFleet.GetModForm(0x80C) as Keyword
    DriverExclusionKeyword = Game.GetFormFromFile(0x3197BE, "Starfield.esm") as Keyword
    ConvoyStarting = false
    LastPlayerVehicle = None
    LastPlayerDriving = false
    ClearDriverReservation()
    StartTimer(0.5, 201)
    StartTimer(1.0, 101)
    Form[] robotKeywords = CassiopeiaPapyrusExtender.GetFormsByEditorID("ActorTypeRobot", true)
    if robotKeywords && robotKeywords.Length > 0
        RobotKeyword = robotKeywords[0] as Keyword
    endif
EndFunction

bool Function DiscoverSavedOccupants()
    EnsureOrderStorage()
    RefreshPassengerForms()
    bool pending = false
    int vehicleIndex = 0
    while ExtraVehicles && vehicleIndex < ExtraVehicles.Length
        ObjectReference vehicle = ExtraVehicles[vehicleIndex]
        if vehicle && !vehicle.Is3DLoaded()
            pending = true
        elseif vehicle && CassiopeiaPapyrusExtender.HasVehicleKeyword(vehicle)
            Furniture base = vehicle.GetBaseObject() as Furniture
            int count = CassiopeiaPapyrusExtender.GetFurnitureMarkerCount(base)
            int marker = 0
            while marker < count && marker < 20
                Actor occupant = CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(vehicle, marker)
                Keyword seat = CassiopeiaPapyrusExtender.GetFurnitureMarkerKeyword(base, marker)
                WriteLog("LOAD_SCAN vehicle=" + vehicle + " marker=" + marker + " occupant=" + occupant + " keyword=" + seat)
                if occupant && occupant != Game.GetPlayer() && !occupant.IsDead() && occupant.GetFurnitureUsing() == vehicle
                    if marker == 0 && seat == Game.GetFormFromFile(0x19, "SFBGS004.esm") as Keyword
                        if !ProbeDriver || ProbeDriver == occupant
                            ProbeDriver = occupant
                            ProbeVehicle = vehicle
                            ProbeDriverKeyword = seat
                            ProbeAddedKeyword = true
                            occupant.AddKeyword(seat)
                            ConvoySession = true
                            ConvoyOrderRequested = true
                            ConvoyOrderDriver = occupant
                            ConvoyOrderVehicle = vehicle
                            if ConvoyKeyword
                                AddedConvoyKeyword = true
                                occupant.AddKeyword(ConvoyKeyword)
                            endif
                            WriteLog("LOAD_SCAN driver order reconstructed actor=" + occupant)
                        endif
                    elseif (marker == 1 && seat == CachedPrimarySeat) || (marker == 3 && seat == CachedFirstRearSeat) || (marker == 4 && seat == CachedSecondRearSeat)
                        int slot = OrderedActors.Find(occupant)
                        if slot < 0
                            slot = OrderedActors.Find(None)
                        endif
                        if slot >= 0
                            bool retainRelease = OrderedActors[slot] == occupant && ReleaseRequested[slot]
                            OrderedActors[slot] = occupant
                            OrderedVehicles[slot] = vehicle
                            OrderedKeywords[slot] = seat
                            AddedOrderKeywords[slot] = true
                            ReleaseRequested[slot] = retainRelease
                            occupant.AddKeyword(seat)
                            if !ConvoyPassengers
                                ConvoyPassengers = new Actor[0]
                            endif
                            if ConvoyPassengers.Find(occupant) < 0
                                ConvoyPassengers.Add(occupant)
                            endif
                            WriteLog("LOAD_SCAN passenger order reconstructed actor=" + occupant + " slot=" + slot)
                        endif
                    endif
                endif
                marker += 1
            endwhile
        endif
        vehicleIndex += 1
    endwhile
    return pending
EndFunction

Function RecoverSavedOccupants()
    LoadRecoveryAttempts += 1
    bool pending = DiscoverSavedOccupants()
    ; Recover an earlier load that cleared driver bookkeeping before exit.
    if !ProbeDriver && DriverPoseOwned && DriverPoseActor
        ObjectReference savedVehicle = DriverPoseActor.GetFurnitureUsing()
        if savedVehicle && ExtraVehicles && ExtraVehicles.Find(savedVehicle) >= 0
            ProbeDriver = DriverPoseActor
            ProbeVehicle = savedVehicle
            ProbeDriverKeyword = CassiopeiaPapyrusExtender.GetFurnitureMarkerKeyword(savedVehicle.GetBaseObject() as Furniture, 0)
            if ProbeDriverKeyword
                ProbeAddedKeyword = true
                ProbeDriver.AddKeyword(ProbeDriverKeyword)
                ConvoySession = ConvoyOrderRequested
                if ConvoySession && ConvoyKeyword
                    AddedConvoyKeyword = true
                endif
            endif
        endif
    endif
    if ProbeDriver && ProbeVehicle && ProbeAddedKeyword && ProbeDriverKeyword
        if !ProbeDriver.Is3DLoaded() || !ProbeVehicle.Is3DLoaded()
            pending = true
        elseif ProbeDriver.GetFurnitureUsing() == ProbeVehicle
            if ConvoySession && ConvoyKeyword && AddedConvoyKeyword
                ProbeDriver.AddKeyword(ConvoyKeyword)
            endif
            bool reboundDriver = ProbeDriver.SnapIntoInteraction(ProbeVehicle)
            WriteLog("LOAD_REBIND driver=" + ProbeDriver + " vehicle=" + ProbeVehicle + " accepted=" + reboundDriver)
            pending = !reboundDriver
        endif
    endif
    int slot = 0
    while OrderedActors && slot < OrderedActors.Length
        Actor passenger = OrderedActors[slot]
        ObjectReference vehicle = OrderedVehicles[slot]
        if passenger && vehicle && (AddedOrderKeywords[slot] || ReleaseRequested[slot]) && OrderedKeywords[slot]
            if !passenger.Is3DLoaded() || !vehicle.Is3DLoaded()
                pending = true
            elseif passenger.GetFurnitureUsing() == vehicle
                if ReleaseRequested[slot] && !AddedOrderKeywords[slot]
                    passenger.AddKeyword(OrderedKeywords[slot])
                    AddedOrderKeywords[slot] = true
                endif
                bool reboundPassenger = passenger.SnapIntoInteraction(vehicle)
                if reboundPassenger && ReleaseRequested[slot]
                    passenger.RemoveKeyword(OrderedKeywords[slot])
                    AddedOrderKeywords[slot] = false
                    passenger.EvaluatePackage()
                endif
                WriteLog("LOAD_REBIND passenger=" + passenger + " vehicle=" + vehicle + " accepted=" + reboundPassenger)
                if !reboundPassenger
                    pending = true
                endif
            endif
            OrderStartTimes[slot] = Utility.GetCurrentRealTime()
        endif
        slot += 1
    endwhile
    if pending && LoadRecoveryAttempts < 20
        StartTimer(1.0, 202)
        return
    endif
    LoadRecoveryPending = false
    WriteLog("LOAD_REBIND completed attempts=" + LoadRecoveryAttempts + " pending=" + pending)
    if ProbeDriver
        ProbeStartRealTime = Utility.GetCurrentRealTime()
        StartTimer(0.5, 115)
    endif
    StartTimer(1.0, 101)
    if RecoveryStopRequested
        EndConvoy()
    endif
EndFunction

Function RememberActiveModel()
    ObjectReference active = GetActiveVehicle()
    if active && active.GetBaseObject()
        if VehicleCatalogue && !VehicleCatalogue.HasForm(active.GetBaseObject())
            VehicleCatalogue.AddForm(active.GetBaseObject())
        endif
        ; Deploy copies the active model, including the standard REV-8.
        AdditionalVehicleBase = active.GetBaseObject()
    endif
EndFunction

Function EnsureGadget()
    if ControlGadget && Game.GetPlayer().GetItemCount(ControlGadget) == 0
        Game.GetPlayer().AddItem(ControlGadget, 1, false)
    endif
    WriteLog("GADGET form=" + ControlGadget + " count=" + Game.GetPlayer().GetItemCount(ControlGadget))
EndFunction

Event Actor.OnPlayerLoadGame(Actor akSender)
    MenuBusy = false
    FleetBusy = false
    LoadRecoveryPending = true
    LoadRecoveryAttempts = 0
    InitializeControl()
    StartTimer(1.0, 202)
EndEvent

Event Actor.OnItemEquipped(Actor akSender, Form akBaseObject, ObjectReference akReference)
    if akSender == Game.GetPlayer() && ControlGadget && akBaseObject == ControlGadget
        Game.GetPlayer().UnequipItem(ControlGadget, false, true)
        OpenActions()
    endif
EndEvent

Function OpenActions()
    if MenuBusy || !ActionMenu
        return
    endif
    MenuBusy = true
    EnsureConvoyRuntime()
    EnsureExtraVehicleNames()
    RememberActiveModel()
    int choice = ActionMenu.Show()
    WriteLog("MENU choice=" + choice)
    if choice == 0
        SpawnAdditionalVehicle()
    elseif choice == 1
        OpenConvoyActions()
    elseif choice == 2
        OpenVehicleUtilities()
    endif
    MenuBusy = false
EndFunction

Function OpenVehicleUtilities()
    Message menu = RV8VehicleFleet.GetModForm(0x80A) as Message
    if !menu
        return
    endif
    int choice = menu.Show()
    if choice == 0
        SelectNextVehicle()
    elseif choice == 1
        RetireSelectedVehicle()
    elseif choice == 2
        ShowFleetStatus()
    elseif choice == 3
        SelectNextFollower()
    elseif choice == 4
        BoardSelectedFollower()
    elseif choice == 5
        DisembarkSelectedFollower()
    elseif choice == 6
        BoardAllRearPassengers()
    elseif choice == 7
        DisembarkAllPassengers()
    endif
EndFunction

Function ReleaseLeadPassengers(ObjectReference lead)
    if !lead || !SupportedConvoyVehicle(lead)
        return
    endif
    int marker = 1
    while marker <= 4
        Actor occupant = CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(lead, marker)
        if occupant && CompatiblePassengerKeyword(lead, marker)
            IssuePassengerExit(occupant, lead)
        endif
        if marker == 1
            marker = 3
        else
            marker += 1
        endif
    endwhile
EndFunction

Function SelectNextModel()
    if !VehicleCatalogue || VehicleCatalogue.GetSize() == 0
        return
    endif
    int index = VehicleCatalogue.Find(AdditionalVehicleBase)
    index += 1
    if index >= VehicleCatalogue.GetSize()
        index = 0
    endif
    AdditionalVehicleBase = VehicleCatalogue.GetAt(index)
    if AdditionalVehicleBase
        Debug.Notification("Vehicle model: " + CassiopeiaPapyrusExtender.GetTESFullName(AdditionalVehicleBase))
    endif
EndFunction

ObjectReference Function GetActiveVehicle()
    return CassiopeiaPapyrusExtender.GetPlayerLandVehicle()
EndFunction

int Function ExtraLimit()
    if Settings
        return Settings.ExtraVehicleLimit
    endif
    return 1
EndFunction

Function SpawnAdditionalVehicle()
    if FleetBusy || !AdditionalVehicleBase
        return
    endif
    FleetBusy = true
    if !ExtraVehicles
        ExtraVehicles = new ObjectReference[0]
    endif
    if ExtraVehicles.Length >= ExtraLimit()
        Debug.Notification("You have reached the additional vehicle limit.")
        FleetBusy = false
        return
    endif
    ObjectReference previousActive = GetActiveVehicle()
    WriteLog("SPAWN before active=" + previousActive + " base=" + AdditionalVehicleBase + " count=" + ExtraVehicles.Length + " limit=" + ExtraLimit())
    float[] offsets = new float[3]
    offsets[0] = 5.0
    offsets[1] = 3.0
    offsets[2] = 0.5
    ObjectReference created = Game.GetPlayer().PlaceAtMe(AdditionalVehicleBase, 1, true, true, false, offsets)
    if created
        ExtraVehicles.Add(created)
        created.Enable()
        LabelAdditionalVehicle(created)
        SelectedVehicle = created
        ; PlaceAtMe is an experimental spawn path. Record whether the engine
        ; reclassified its active vehicle instead of assuming independence.
        WriteLog("SPAWN created=" + created + " activeBefore=" + previousActive + " activeAfter=" + GetActiveVehicle() + " loaded=" + created.Is3DLoaded())
        StartTimer(2.0, 100)
        Debug.Notification("Additional vehicle deployed.")
    else
        Debug.Notification("The vehicle could not be deployed.")
    endif
    FleetBusy = false
EndFunction

Event OnTimer(int aiTimerID)
    if aiTimerID == 202
        RecoverSavedOccupants()
    elseif aiTimerID == 201
        UpdatePlayerAcceleration()
        if !LoadRecoveryPending
            ObserveConvoyAutomation()
        endif
        StartTimer(0.5, 201)
    elseif aiTimerID == 115
        SampleDriverProbe()
    elseif aiTimerID == 101
        ObserveOrders()
    elseif aiTimerID == 100
        ObjectReference active = GetActiveVehicle()
        WriteLog("SPAWN delayed active=" + active + " extraCount=" + ExtraVehicles.Length)
        int index = 0
        while ExtraVehicles && index < ExtraVehicles.Length
            ObjectReference vehicle = ExtraVehicles[index]
            if vehicle
                WriteLog("EXTRA index=" + index + " ref=" + vehicle + " base=" + vehicle.GetBaseObject() + " loaded=" + vehicle.Is3DLoaded() + " vehicleKeyword=" + CassiopeiaPapyrusExtender.HasVehicleKeyword(vehicle))
            endif
            index += 1
        endwhile
    endif
EndEvent

Function SelectNextVehicle()
    ObjectReference active = GetActiveVehicle()
    if !SelectedVehicle || SelectedVehicle == active
        if ExtraVehicles && ExtraVehicles.Length > 0
            SelectedVehicle = ExtraVehicles[0]
        else
            SelectedVehicle = active
        endif
    else
        int index = ExtraVehicles.Find(SelectedVehicle)
        if index >= 0 && index + 1 < ExtraVehicles.Length
            SelectedVehicle = ExtraVehicles[index + 1]
        else
            SelectedVehicle = active
        endif
    endif
    ShowFleetStatus()
EndFunction

bool Function HasOccupants(ObjectReference vehicle)
    if !vehicle
        return false
    endif
    if ProbeDriver && ProbeDriver.GetFurnitureUsing() == vehicle
        return true
    endif
    int ownedSlot = 0
    while OrderedActors && ownedSlot < OrderedActors.Length
        if OrderedActors[ownedSlot] && OrderedActors[ownedSlot].GetFurnitureUsing() == vehicle
            return true
        endif
        ownedSlot += 1
    endwhile
    if Game.GetPlayer().GetFurnitureUsing() == vehicle
        return true
    endif
    int marker = 0
    Furniture base = vehicle.GetBaseObject() as Furniture
    int markerCount = CassiopeiaPapyrusExtender.GetFurnitureMarkerCount(base)
    if markerCount < 0 || markerCount > 20
        return true
    endif
    while marker < markerCount
        if vehicle.IsFurnitureMarkerInUse(marker, false)
            return true
        endif
        marker += 1
    endwhile
    return false
EndFunction

Function RetireSelectedVehicle()
    if FleetBusy || !SelectedVehicle || !ExtraVehicles
        return
    endif
    int ownedIndex = ExtraVehicles.Find(SelectedVehicle)
    if ownedIndex < 0 || SelectedVehicle == GetActiveVehicle()
        Debug.Notification("Select an additional vehicle to recall.")
        return
    endif
    FleetBusy = true
    if HasOccupants(SelectedVehicle)
        WriteLog("RECALL blocked occupied=" + SelectedVehicle)
        Debug.Notification("All occupants must leave the vehicle first.")
        FleetBusy = false
        return
    endif
    ObjectReference retiring = SelectedVehicle
    WriteLog("RECALL requested=" + retiring)
    retiring.Disable()
    ExtraVehicles.Remove(ownedIndex)
    PruneVehicleNameReferences()
    SelectedVehicle = GetActiveVehicle()
    retiring.Delete()
    WriteLog("RECALL complete count=" + ExtraVehicles.Length + " active=" + GetActiveVehicle())
    FleetBusy = false
EndFunction

Function ShowFleetStatus()
    int count = 0
    if ExtraVehicles
        count = ExtraVehicles.Length
    endif
    string selectedName = "None"
    if SelectedVehicle && SelectedVehicle.GetBaseObject()
        selectedName = CassiopeiaPapyrusExtender.GetReferenceName(SelectedVehicle)
    endif
    if SelectedVehicle
        int vehicleIndex = ExtraVehicles.Find(SelectedVehicle)
        if vehicleIndex >= 0
            selectedName = "Additional vehicle " + (vehicleIndex + 1) + ": " + selectedName
        elseif SelectedVehicle == GetActiveVehicle()
            selectedName = "Active vehicle: " + selectedName
        endif
    endif
    string followerName = "None"
    if SelectedFollower
        followerName = CassiopeiaPapyrusExtender.GetReferenceName(SelectedFollower)
    endif
    Debug.MessageBox("Additional vehicles: " + count + "\nSelected vehicle: " + selectedName + "\nSelected follower: " + followerName)
EndFunction


Function EnsureOrderStorage()
    if !OrderedActors
        OrderedActors = new Actor[8]
        OrderedVehicles = new ObjectReference[8]
        OrderedKeywords = new Keyword[8]
        AddedOrderKeywords = new bool[8]
        ReleaseRequested = new bool[8]
        OrderStartTimes = new float[8]
    endif
EndFunction

Actor[] Function ReadFollowers()
    Quest followerQuest = Game.GetFormFromFile(0xD773, "Starfield.esm") as Quest
    if followerQuest
        RefCollectionAlias activeFollowers = followerQuest.GetAlias(17) as RefCollectionAlias
        if activeFollowers
            Actor[] followers = activeFollowers.GetArray() as Actor[]
            if followers && followers.Length > 0
                return followers
            endif
        endif
    endif
    return Game.GetPlayerFollowers()
EndFunction

Function SelectNextFollower()
    Actor[] followers = ReadFollowers()
    if !followers || followers.Length == 0
        Debug.Notification("No followers are available.")
        return
    endif
    int next = followers.Find(SelectedFollower) + 1
    int checked = 0
    while checked < followers.Length
        if next >= followers.Length
            next = 0
        endif
        Actor candidate = followers[next]
        if candidate && candidate != Game.GetPlayer() && candidate.Is3DLoaded() && !candidate.IsDead()
            if !RobotKeyword || !candidate.HasKeyword(RobotKeyword)
                SelectedFollower = candidate
                Debug.Notification("Selected follower: " + CassiopeiaPapyrusExtender.GetReferenceName(candidate))
                WriteLog("FOLLOWER selected=" + candidate)
                return
            endif
        endif
        next += 1
        checked += 1
    endwhile
    Debug.Notification("No compatible followers are available.")
EndFunction

bool Function SupportedConvoyVehicle(ObjectReference vehicle)
    if !vehicle || !vehicle.Is3DLoaded() || !CassiopeiaPapyrusExtender.HasVehicleKeyword(vehicle)
        return false
    endif
    Furniture base = vehicle.GetBaseObject() as Furniture
    Keyword driver = Game.GetFormFromFile(0x19, "SFBGS004.esm") as Keyword
    int count = CassiopeiaPapyrusExtender.GetFurnitureMarkerCount(base)
    return base && driver && count > 0 && count <= 20 && CassiopeiaPapyrusExtender.GetFurnitureMarkerKeyword(base, 0) == driver
EndFunction

Keyword Function CompatiblePassengerKeyword(ObjectReference vehicle, int marker)
    if !SupportedConvoyVehicle(vehicle)
        return None
    endif
    Furniture base = vehicle.GetBaseObject() as Furniture
    int count = CassiopeiaPapyrusExtender.GetFurnitureMarkerCount(base)
    if marker < 0 || marker >= count
        return None
    endif
    Keyword expected = None
    if marker == 1
        expected = CachedPrimarySeat
    elseif marker == 3
        expected = CachedFirstRearSeat
    elseif marker == 4
        expected = CachedSecondRearSeat
    endif
    if expected && CassiopeiaPapyrusExtender.GetFurnitureMarkerKeyword(base, marker) == expected
        return expected
    endif
    return None
EndFunction

int Function FreeHumanPassengerSeats(ObjectReference vehicle)
    int available = 0
    int marker = 1
    while marker <= 4
        Keyword seat = CompatiblePassengerKeyword(vehicle, marker)
        if seat && !vehicle.IsFurnitureMarkerInUse(marker, false)
            bool reserved = false
            int slot = 0
            while OrderedActors && slot < OrderedActors.Length
                if OrderedActors[slot] && OrderedVehicles[slot] == vehicle && OrderedKeywords[slot] == seat
                    reserved = true
                endif
                slot += 1
            endwhile
            if !reserved
                available += 1
            endif
        endif
        if marker == 1
            marker = 3
        else
            marker += 1
        endif
    endwhile
    return available
EndFunction

Function BoardSelectedFollower()
    EnsureOrderStorage()
    if !SelectedVehicle || !SelectedVehicle.Is3DLoaded() || !SelectedFollower || !SelectedFollower.Is3DLoaded()
        Debug.Notification("Select a nearby vehicle and follower first.")
        return
    endif
    Actor[] followers = ReadFollowers()
    if !followers || followers.Find(SelectedFollower) < 0 || SelectedFollower.IsDead() || SelectedFollower.IsInCombat()
        Debug.Notification("This follower cannot board right now.")
        return
    endif
    if SelectedFollower.GetFurnitureUsing() || OrderedActors.Find(SelectedFollower) >= 0
        Debug.Notification("This follower is already seated or has a boarding order.")
        return
    endif
    if !CachedPrimarySeat || !CachedFirstRearSeat || !CachedSecondRearSeat
        RefreshPassengerForms()
    endif
    if !SupportedConvoyVehicle(SelectedVehicle)
        Debug.Notification("This vehicle does not expose a compatible driver and passenger layout.")
        return
    endif
    Keyword primary = CachedPrimarySeat
    Keyword first = CachedFirstRearSeat
    Keyword second = CachedSecondRearSeat
    if !primary || !first || !second || SelectedFollower.HasKeyword(primary) || SelectedFollower.HasKeyword(first) || SelectedFollower.HasKeyword(second)
        Debug.Notification("This follower already has a seat assignment.")
        return
    endif
    ; The lead's standard front seat remains under normal companion selection.
    int marker = 1
    if Game.GetPlayer().GetFurnitureUsing() == SelectedVehicle
        marker = 3
    endif
    while marker <= 4
        Keyword seat = CompatiblePassengerKeyword(SelectedVehicle, marker)
        bool reserved = !seat
        if seat
            reserved = SelectedVehicle.IsFurnitureMarkerInUse(marker, false)
        endif
        int existing = 0
        while existing < OrderedActors.Length
            if OrderedActors[existing] && OrderedVehicles[existing] == SelectedVehicle
                Keyword markerKeyword = seat
                if OrderedKeywords[existing] == markerKeyword
                    reserved = true
                endif
            endif
            existing += 1
        endwhile
        if !reserved
            int slot = OrderedActors.Find(None)
            if slot < 0
                Debug.Notification("No additional passenger orders are available.")
                return
            endif
            Keyword selected = seat
            OrderedActors[slot] = SelectedFollower
            OrderedVehicles[slot] = SelectedVehicle
            OrderedKeywords[slot] = selected
            AddedOrderKeywords[slot] = true
            ReleaseRequested[slot] = false
            OrderStartTimes[slot] = Utility.GetCurrentRealTime()
            SelectedFollower.AddKeyword(selected)
            if !SelectedFollower.HasKeyword(selected)
                ClearOrder(slot)
                Debug.Notification("The boarding order could not be issued.")
                return
            endif
            bool requested = SelectedFollower.SnapIntoInteraction(SelectedVehicle)
            WriteLog("ORDER board actor=" + SelectedFollower + " vehicle=" + SelectedVehicle + " marker=" + marker + " result=" + requested)
            if !requested
                ClearOrder(slot)
                Debug.Notification("This seat could not accept the follower. Check the vehicle's interaction markers.")
                return
            endif
            Debug.Notification("Boarding order issued.")
            StartTimer(1.0, 101)
            return
        endif
        if marker == 1
            marker = 3
        else
            marker += 1
        endif
    endwhile
    Debug.Notification("No compatible human seats are available. Deploy another vehicle or leave followers behind.")
EndFunction

Function ClearOrder(int slot)
    if OrderedActors[slot] && OrderedKeywords[slot] && AddedOrderKeywords[slot]
        OrderedActors[slot].RemoveKeyword(OrderedKeywords[slot])
        WriteLog("ORDER keyword removed actor=" + OrderedActors[slot])
    endif
    OrderedActors[slot] = None
    OrderedVehicles[slot] = None
    OrderedKeywords[slot] = None
    AddedOrderKeywords[slot] = false
    ReleaseRequested[slot] = false
    OrderStartTimes[slot] = 0.0
EndFunction

Function DisembarkSelectedFollower()
    Actor candidate = SelectedFollower
    ObjectReference lead = Game.GetPlayer().GetFurnitureUsing()
    if !lead || !SupportedConvoyVehicle(lead)
        lead = GetActiveVehicle()
    endif
    if !candidate || !candidate.GetFurnitureUsing()
        if lead
            candidate = CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(lead, 1)
        endif
    endif
    if candidate && IssuePassengerExit(candidate, candidate.GetFurnitureUsing())
        Debug.Notification("Disembarking order issued.")
    else
        Debug.Notification("No seated human follower is available for this order.")
    endif
EndFunction

Function ObserveOrders()
    if LoadRecoveryPending
        StartTimer(1.0, 101)
        return
    endif
    EnsureOrderStorage()
    bool pending = false
    int slot = 0
    while slot < OrderedActors.Length
        Actor passenger = OrderedActors[slot]
        if passenger
            ObjectReference vehicle = OrderedVehicles[slot]
            float elapsed = Utility.GetCurrentRealTime() - OrderStartTimes[slot]
            if passenger.IsDead() || passenger.IsInCombat() || !vehicle || !vehicle.Is3DLoaded()
                ClearOrder(slot)
                passenger.EvaluatePackage()
            elseif passenger.GetFurnitureUsing() == vehicle
                pending = true
                WriteLog("ORDER seated actor=" + passenger + " vehicle=" + vehicle + " sitState=" + passenger.GetSitState() + " release=" + ReleaseRequested[slot])
            elseif ReleaseRequested[slot] || elapsed > 10.0
                WriteLog("ORDER ended actor=" + passenger + " furniture=" + passenger.GetFurnitureUsing() + " elapsed=" + elapsed)
                ClearOrder(slot)
            else
                pending = true
            endif
        endif
        slot += 1
    endwhile
    if pending
        StartTimer(1.0, 101)
    endif
EndFunction

Function DriverProbe() Global
    RV8VehicleFleet fleet = RV8VehicleFleet.GetModForm(0x809) as RV8VehicleFleet
    if fleet
        fleet.StartDriverProbe()
    endif
EndFunction

Function StartDriverProbe(bool convoy = false)
    if ProbeDriver
        Debug.Notification("A driving session is already running.")
        return
    endif
    Actor candidate = SelectedFollower
    ObjectReference vehicle = SelectedVehicle
    bool supported = SupportedConvoyVehicle(vehicle)
    Keyword driverKeyword = Game.GetFormFromFile(0x19, "SFBGS004.esm") as Keyword
    if !candidate || !vehicle || !supported || !driverKeyword
        Debug.Notification("Select a follower and a compatible vehicle first.")
        return
    endif
    if candidate == Game.GetPlayer() || candidate.IsDead() || candidate.IsInCombat() || !candidate.Is3DLoaded() || !vehicle.Is3DLoaded() || !supported
        Debug.Notification("This follower or vehicle is unavailable for the driver test.")
        return
    endif
    if candidate.GetFurnitureUsing() || candidate.HasKeyword(driverKeyword) || vehicle.IsFurnitureMarkerInUse(0, false) || vehicle == GetActiveVehicle() || Game.GetPlayer().GetFurnitureUsing() == vehicle
        Debug.Notification("Use an empty additional vehicle and an unseated follower.")
        return
    endif
    if convoy && (!ConvoyKeyword || candidate.HasKeyword(ConvoyKeyword))
        Debug.Notification("The convoy order is unavailable or already owned.")
        return
    endif
    ConvoySession = convoy
    if convoy
        candidate.AddKeyword(ConvoyKeyword)
        AddedConvoyKeyword = candidate.HasKeyword(ConvoyKeyword)
        if !AddedConvoyKeyword
            ConvoySession = false
            return
        endif
        UpdateConvoyLights(candidate)
    endif
    ProbeDriver = candidate
    ProbeVehicle = vehicle
    ProbeDriverKeyword = driverKeyword
    ProbeAddedKeyword = true
    ProbeSamples = 0
    ProbeStartRealTime = Utility.GetCurrentRealTime()
    candidate.AddKeyword(driverKeyword)
    if !candidate.HasKeyword(driverKeyword)
        ClearConvoyKeywords(candidate)
        ConvoySession = false
        ProbeDriver = None
        ProbeVehicle = None
        ProbeDriverKeyword = None
        ProbeAddedKeyword = false
        Debug.Notification("The driver order could not be issued.")
        return
    endif
    bool entered = candidate.SnapIntoInteraction(vehicle)
    WriteLog("DRIVER start actor=" + candidate + " vehicle=" + vehicle + " snap=" + entered)
    if convoy
        Debug.Notification("Convoy started.")
    else
        Debug.Notification("Driving session started. Stay ahead on clear, flat ground. Automatic release at 30 seconds.")
    endif
    StartTimer(0.5, 115)
EndFunction

; FurnitureRover_Wrapping selects its NPC/Player animation branch using iIsPlayer.
; This is a graph variable only: do not alter Actor identity, player globals,
; model assets, passenger keywords or native control ownership.
Function RestoreDriverPose()
    DriverPoseExitPending = false
    Actor previous = DriverPoseActor
    int original = DriverPosePrevious
    bool owned = DriverPoseOwned
    DriverPoseActor = None
    DriverPoseOwned = false
    if previous && owned && previous.Is3DLoaded()
        int current = previous.GetAnimationVariableInt("iIsPlayer")
        if current == 1
            previous.SetAnimationVariableInt("iIsPlayer", original)
            WriteLog("DRIVER POSE restore actor=" + previous + " original=" + original + " readback=" + previous.GetAnimationVariableInt("iIsPlayer"))
        else
            WriteLog("DRIVER POSE restore skipped external value actor=" + previous + " value=" + current)
        endif
    endif
EndFunction

Function DeferDriverPoseRestore()
    if DriverPoseActor && DriverPoseOwned && DriverPoseActor.Is3DLoaded() && DriverPoseActor.GetSitState() != 0
        DriverPoseExitPending = true
        WriteLog("DRIVER POSE exit pending actor=" + DriverPoseActor + " state=" + DriverPoseActor.GetSitState())
    else
        RestoreDriverPose()
    endif
EndFunction

Function ObserveDriverPoseExit()
    if DriverPoseExitPending && DriverPoseActor && DriverPoseActor.Is3DLoaded()
        if DriverPoseActor.GetSitState() == 0 || DriverPoseActor.IsDead()
            RestoreDriverPose()
        endif
    endif
EndFunction

Function UpdateDriverPose()
    if DriverPoseExitPending
        ObserveDriverPoseExit()
        return
    endif
    Actor candidate = ProbeDriver
    ObjectReference vehicle = ProbeVehicle
    bool valid = candidate && vehicle && candidate != Game.GetPlayer() && candidate.Is3DLoaded() && vehicle.Is3DLoaded() && !candidate.IsDead() && ProbeAddedKeyword && ProbeDriverKeyword && candidate.HasKeyword(ProbeDriverKeyword)
    if valid
        valid = candidate.GetSitState() == 3 && candidate.GetFurnitureUsing() == vehicle && SupportedConvoyVehicle(vehicle)
    endif
    if !valid
        if DriverPoseActor
            RestoreDriverPose()
        endif
        return
    endif
    if DriverPoseActor && DriverPoseActor != candidate
        RestoreDriverPose()
    endif
    int current = candidate.GetAnimationVariableInt("iIsPlayer")
    if !DriverPoseActor
        DriverPoseActor = candidate
        DriverPosePrevious = current
        DriverPoseOwned = current == 0
        DriverPoseLastLog = 0.0
        WriteLog("DRIVER POSE begin actor=" + candidate + " vehicle=" + vehicle + " original=" + current + " owned=" + DriverPoseOwned)
    endif
    if DriverPoseOwned && current == 0
        candidate.SetAnimationVariableInt("iIsPlayer", 1)
    endif
    if Utility.GetCurrentRealTime() - DriverPoseLastLog >= 5.0
        DriverPoseLastLog = Utility.GetCurrentRealTime()
        WriteLog("DRIVER POSE sample actor=" + candidate + " prior=" + current + " readback=" + candidate.GetAnimationVariableInt("iIsPlayer") + " sitState=" + candidate.GetSitState() + " furniture=" + candidate.GetFurnitureUsing())
    endif
EndFunction

Function SampleDriverProbe()
    if LoadRecoveryPending
        StartTimer(1.0, 115)
        return
    endif
    UpdateDriverPose()
    if !ProbeDriver || !ProbeVehicle
        return
    endif
    if ConvoySession
        ObjectReference playerVehicle = Game.GetPlayer().GetFurnitureUsing()
        bool valid = ProbeDriver.Is3DLoaded() && ProbeVehicle.Is3DLoaded() && !ProbeDriver.IsDead()
        if !valid || !AddedConvoyKeyword
            EndConvoy(false)
            return
        endif
        ; Re-query only the owned keyword. Native driver holding expires if this heartbeat stops.
        ; Re-adding an existing keyword is idempotent in the native mutation path.
        ; It also delivers the heartbeat through the verified AddKeyword hook.
        ProbeDriver.AddKeyword(ConvoyKeyword)
        if !ProbeDriver.HasKeyword(ConvoyKeyword)
            EndConvoy(false)
            return
        endif
        UpdateConvoyLights(ProbeDriver)
        UpdateConvoyActionPolicies(ProbeDriver)
        ProbeSamples += 1
        if ProbeSamples % 4 == 0 && playerVehicle
            WriteLog("CONVOY sample actor=" + ProbeDriver + " lead=" + playerVehicle + " extra=" + ProbeVehicle + " state=" + ProbeDriver.GetSitState() + " distance=" + ProbeVehicle.GetDistance(playerVehicle))
        endif
        if ProbeSamples > 24 && ProbeDriver.GetFurnitureUsing() != ProbeVehicle
            WriteLog("CONVOY paused: driver did not occupy assigned vehicle")
            EndConvoy(false)
            return
        endif
        StartTimer(0.5, 115)
        return
    endif
    ProbeSamples += 1
    WriteLog("DRIVER elapsed=" + (Utility.GetCurrentRealTime() - ProbeStartRealTime) + " package=" + ProbeDriver.GetCurrentPackage() + " sample=" + ProbeSamples + " state=" + ProbeDriver.GetSitState() + " furniture=" + ProbeDriver.GetFurnitureUsing() + " driverUser=" + CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(ProbeVehicle, 0) + " vehicleX=" + ProbeVehicle.GetPositionX() + " vehicleY=" + ProbeVehicle.GetPositionY() + " vehicleZ=" + ProbeVehicle.GetPositionZ() + " actorX=" + ProbeDriver.GetPositionX() + " actorY=" + ProbeDriver.GetPositionY() + " playerDistance=" + ProbeVehicle.GetDistance(Game.GetPlayer()))
    if (Utility.GetCurrentRealTime() - ProbeStartRealTime) < 31.0 && ProbeSamples < 70 && ProbeDriver.Is3DLoaded() && ProbeVehicle.Is3DLoaded() && !ProbeDriver.IsDead() && !ProbeDriver.IsInCombat()
        StartTimer(0.5, 115)
    else
        if ProbeAddedKeyword && ProbeDriverKeyword
            ProbeDriver.RemoveKeyword(ProbeDriverKeyword)
        endif
        ProbeDriver.EvaluatePackage()
        RestoreDriverPose()
        WriteLog("DRIVER observation ended; automatic release requested")
        ProbeDriver = None
        ProbeVehicle = None
        ProbeDriverKeyword = None
        ProbeAddedKeyword = false
        Debug.Notification("Driving session complete.")
    endif
EndFunction

; Keep only currently owned vehicles. This also releases historical naming
; references retained by earlier controller versions when a save is loaded.
Function PruneVehicleNameReferences()
    ObjectReference[] retained = new ObjectReference[0]
    int index = 0
    int previousCount = 0
    if NamedExtraVehicles
        previousCount = NamedExtraVehicles.Length
    endif
    while NamedExtraVehicles && index < NamedExtraVehicles.Length
        ObjectReference candidate = NamedExtraVehicles[index]
        if candidate && ExtraVehicles && ExtraVehicles.Find(candidate) >= 0 && retained.Find(candidate) < 0
            retained.Add(candidate)
        endif
        index += 1
    endwhile
    NamedExtraVehicles = retained
    if previousCount != retained.Length
        WriteLog("CLEANUP naming references before=" + previousCount + " after=" + retained.Length)
    endif
EndFunction

Function EnsureExtraVehicleNames()
    PruneVehicleNameReferences()
    if !NamedExtraVehicles
        NamedExtraVehicles = new ObjectReference[0]
    endif
    int index = 0
    while ExtraVehicles && index < ExtraVehicles.Length
        LabelAdditionalVehicle(ExtraVehicles[index])
        index += 1
    endwhile
EndFunction

Function LabelAdditionalVehicle(ObjectReference vehicle)
    if !vehicle || vehicle == GetActiveVehicle()
        return
    endif
    if !NamedExtraVehicles
        NamedExtraVehicles = new ObjectReference[0]
    endif
    if NamedExtraVehicles.Find(vehicle) >= 0
        return
    endif
    string modelName = CassiopeiaPapyrusExtender.GetTESFullName(vehicle.GetBaseObject())
    if modelName == ""
        modelName = "Vehicle"
    endif
    string label = modelName + " [Additional #" + NextExtraVehicleNumber + "]"
    bool renamed = CassiopeiaPapyrusExtender.SetDisplayName(vehicle, label)
    WriteLog("EXTRA name=" + label + " vehicle=" + vehicle + " result=" + renamed)
    if renamed
        NamedExtraVehicles.Add(vehicle)
        NextExtraVehicleNumber += 1
    endif
EndFunction


Function OpenDrivingActions()
    Message actions = RV8VehicleFleet.GetModForm(0x80A) as Message
    if !actions
        return
    endif
    int choice = actions.Show()
    if choice == 0
        BoardAllRearPassengers()
    elseif choice == 1
        DisembarkAllPassengers()
    elseif choice == 2
        OpenConvoyActions()
    endif
EndFunction

Function StopDriverOrder()
    DeferDriverPoseRestore()
    if ConvoySession || ConvoyOrderRequested
        EndConvoy()
        return
    endif
    if ProbeDriver && ProbeDriverKeyword && ProbeAddedKeyword
        ProbeDriver.RemoveKeyword(ProbeDriverKeyword)
        ProbeAddedKeyword = false
        ProbeDriver.EvaluatePackage()
        WriteLog("DRIVER explicit stop actor=" + ProbeDriver + " vehicle=" + ProbeVehicle)
        Debug.Notification("Stopping the additional vehicle. Driver release requested.")
    endif
EndFunction

Function BoardAllRearPassengers()
    if BoardingGroupBusy
        return
    endif
    BoardingGroupBusy = true
    Actor previous = SelectedFollower
    Actor[] followers = ReadFollowers()
    int i = 0
    while followers && i < followers.Length
        Actor candidate = followers[i]
        if candidate && candidate != Game.GetPlayer() && candidate.Is3DLoaded() && !candidate.IsDead() && !candidate.IsInCombat() && !candidate.GetFurnitureUsing()
            if !RobotKeyword || !candidate.HasKeyword(RobotKeyword)
                SelectedFollower = candidate
                BoardSelectedFollower()
                Utility.Wait(0.25)
            endif
        endif
        i += 1
    endwhile
    SelectedFollower = previous
    BoardingGroupBusy = false
EndFunction

Function DisembarkAllPassengers()
    EnsureOrderStorage()
    Actor previous = SelectedFollower
    int i = 0
    while i < OrderedActors.Length
        if OrderedActors[i] && OrderedVehicles[i] == SelectedVehicle
            SelectedFollower = OrderedActors[i]
            DisembarkSelectedFollower()
        endif
        i += 1
    endwhile
    SelectedFollower = previous
EndFunction

Function RefreshPassengerForms()
    CachedPrimarySeat = Game.GetFormFromFile(0x1D, "SFBGS004.esm") as Keyword
    CachedFirstRearSeat = RV8VehicleFleet.GetModForm(0x822) as Keyword
    CachedSecondRearSeat = RV8VehicleFleet.GetModForm(0x823) as Keyword
EndFunction

bool Function SelectableConvoyDriver(Actor candidate)
    EnsureOrderStorage()
    if !candidate || candidate == Game.GetPlayer() || candidate.IsDead() || candidate.IsInCombat() || !candidate.Is3DLoaded()
        return false
    endif
    if RobotKeyword && candidate.HasKeyword(RobotKeyword)
        return false
    endif
    Actor[] followers = ReadFollowers()
    if !followers || followers.Find(candidate) < 0
        return false
    endif
    ObjectReference occupiedVehicle = candidate.GetFurnitureUsing()
    if !occupiedVehicle
        return candidate.GetSitState() == 0
    endif
    if !SupportedConvoyVehicle(occupiedVehicle)
        return false
    endif
    int marker = 1
    while marker <= 4
        if CompatiblePassengerKeyword(occupiedVehicle, marker) && CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(occupiedVehicle, marker) == candidate
            return true
        endif
        if marker == 1
            marker = 3
        else
            marker += 1
        endif
    endwhile
    return false
EndFunction

Actor Function ChooseRequestedDriver()
    if SelectableConvoyDriver(AssignedDriver)
        return AssignedDriver
    endif
    if SelectableConvoyDriver(SelectedFollower)
        return SelectedFollower
    endif
    Actor[] followers = ReadFollowers()
    int index = 0
    while followers && index < followers.Length
        if SelectableConvoyDriver(followers[index])
            return followers[index]
        endif
        index += 1
    endwhile
    return None
EndFunction

bool Function IssuePassengerExit(Actor candidate, ObjectReference vehicle)
    EnsureOrderStorage()
    if !candidate || !vehicle || candidate == Game.GetPlayer() || !candidate.Is3DLoaded() || !vehicle.Is3DLoaded() || candidate.GetFurnitureUsing() != vehicle || candidate.GetSitState() != 3 || !SupportedConvoyVehicle(vehicle)
        return false
    endif
    Actor[] followers = ReadFollowers()
    if !followers || followers.Find(candidate) < 0 || candidate.IsDead() || (RobotKeyword && candidate.HasKeyword(RobotKeyword))
        return false
    endif
    Keyword seat = None
    int marker = 1
    while marker <= 4 && !seat
        if CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(vehicle, marker) == candidate
            seat = CompatiblePassengerKeyword(vehicle, marker)
        endif
        if marker == 1
            marker = 3
        else
            marker += 1
        endif
    endwhile
    Keyword releaseSignal = RV8VehicleFleet.GetModForm(0x824) as Keyword
    if !seat || !releaseSignal
        return false
    endif
    int slot = OrderedActors.Find(candidate)
    bool newOrder = slot < 0
    if !newOrder && (OrderedVehicles[slot] != vehicle || OrderedKeywords[slot] != seat)
        return false
    endif
    if slot < 0
        slot = OrderedActors.Find(None)
        if slot < 0
            return false
        endif
        OrderedActors[slot] = candidate
        OrderedVehicles[slot] = vehicle
        OrderedKeywords[slot] = seat
        AddedOrderKeywords[slot] = !candidate.HasKeyword(seat)
    endif
    if AddedOrderKeywords[slot]
        candidate.AddKeyword(seat)
    endif
    bool bound = candidate.SnapIntoInteraction(vehicle)
    if !bound
        WriteLog("PASSENGER EXIT registration failed actor=" + candidate)
        if newOrder
            ClearOrder(slot)
        endif
        return false
    endif
    bool ownSignal = !candidate.HasKeyword(releaseSignal)
    candidate.AddKeyword(releaseSignal)
    bool sent = candidate.HasKeyword(releaseSignal)
    if ownSignal
        candidate.RemoveKeyword(releaseSignal)
    endif
    if AddedOrderKeywords[slot]
        candidate.RemoveKeyword(seat)
        AddedOrderKeywords[slot] = false
    endif
    ReleaseRequested[slot] = true
    OrderStartTimes[slot] = Utility.GetCurrentRealTime()
    candidate.EvaluatePackage()
    StartTimer(1.0, 101)
    WriteLog("PASSENGER EXIT signal actor=" + candidate + " vehicle=" + vehicle + " issued=" + sent)
    return sent
EndFunction

Function RequestManualConvoy()
    if ConvoySession || ConvoyStarting || ProbeDriver
        Debug.Notification("A convoy is already active. Stop it before changing drivers.")
        return
    endif
    ObjectReference extra = SelectedVehicle
    if !extra || !extra.Is3DLoaded() || !ExtraVehicles || ExtraVehicles.Find(extra) < 0 || extra == GetActiveVehicle() || HasOccupants(extra) || !SupportedConvoyVehicle(extra)
        extra = AvailableConvoyVehicle()
    endif
    Actor candidate = ChooseRequestedDriver()
    if !extra
        Debug.Notification("Deploy an empty compatible additional vehicle first.")
        return
    endif
    if !candidate
        Debug.Notification("No available human follower can drive the convoy vehicle.")
        return
    endif
    ConvoyOrderRequested = true
    ConvoyOrderDriver = candidate
    ConvoyOrderVehicle = extra
    LastConvoyResumeAttempt = 0.0
    ReserveConvoyDriver(candidate)
    ObjectReference current = candidate.GetFurnitureUsing()
    if current
        DriverTransferPending = IssuePassengerExit(candidate, current)
        DriverTransferStart = Utility.GetCurrentRealTime()
        if DriverTransferPending
            Debug.Notification("Driver transfer ordered. Waiting for the follower to leave the current seat.")
        else
            ConvoyOrderRequested = false
            ClearDriverReservation()
            Debug.Notification("The follower could not leave the current seat. Convoy order was not started.")
        endif
    elseif EligibleConvoyDriver(candidate)
        ObjectReference lead = Game.GetPlayer().GetFurnitureUsing()
        if lead && Game.GetPlayer().GetSitState() == 3 && CassiopeiaPapyrusExtender.HasVehicleKeyword(lead)
            BeginConvoy(candidate, extra)
        else
            Debug.Notification("Convoy ready. Your assigned driver will board when you enter the lead vehicle.")
        endif
    endif
EndFunction

Function CycleRequestedDriver()
    if ConvoySession || ConvoyStarting || ProbeDriver
        Debug.Notification("Stop the convoy before changing its driver.")
        return
    endif
    Actor[] followers = ReadFollowers()
    if !followers || followers.Length == 0
        Debug.Notification("No human follower is available.")
        return
    endif
    int start = followers.Find(AssignedDriver)
    int count = 0
    while count < followers.Length
        start += 1
        if start >= followers.Length
            start = 0
        endif
        if SelectableConvoyDriver(followers[start])
            AssignedDriver = followers[start]
            if ConvoyOrderRequested
                ConvoyOrderDriver = AssignedDriver
                ReserveConvoyDriver(AssignedDriver)
            endif
            Debug.Notification("Convoy driver: " + CassiopeiaPapyrusExtender.GetReferenceName(AssignedDriver))
            return
        endif
        count += 1
    endwhile
    Debug.Notification("No human follower is available.")
EndFunction

bool Function EligibleConvoyDriver(Actor candidate)
    if !candidate || candidate == Game.GetPlayer() || candidate.IsDead() || candidate.IsInCombat() || !candidate.Is3DLoaded() || candidate.GetFurnitureUsing() || candidate.GetSitState() != 0
        return false
    endif
    if RobotKeyword && candidate.HasKeyword(RobotKeyword)
        return false
    endif
    Actor[] followers = ReadFollowers()
    return followers && followers.Find(candidate) >= 0 && OrderedActors.Find(candidate) < 0
EndFunction

ObjectReference Function AvailableConvoyVehicle()
    int index = 0
    while ExtraVehicles && index < ExtraVehicles.Length
        ObjectReference vehicle = ExtraVehicles[index]
        if vehicle && vehicle != GetActiveVehicle() && vehicle.Is3DLoaded() && SupportedConvoyVehicle(vehicle) && !HasOccupants(vehicle)
            return vehicle
        endif
        index += 1
    endwhile
    return None
EndFunction

Actor Function ChooseConvoyDriver()
    if AssignedDriver
        if EligibleConvoyDriver(AssignedDriver)
            return AssignedDriver
        endif
        return None
    endif
    Actor[] followers = ReadFollowers()
    int index = 0
    while followers && index < followers.Length
        if EligibleConvoyDriver(followers[index])
            return followers[index]
        endif
        index += 1
    endwhile
    return None
EndFunction

Function ClearDriverReservation()
    if ReservedDriver && AddedDriverExclusion && DriverExclusionKeyword
        ReservedDriver.RemoveKeyword(DriverExclusionKeyword)
    endif
    ReservedDriver = None
    AddedDriverExclusion = false
EndFunction

Function ReserveConvoyDriver(Actor candidate)
    if ReservedDriver == candidate
        return
    endif
    ClearDriverReservation()
    if candidate && DriverExclusionKeyword
        ReservedDriver = candidate
        if !candidate.HasKeyword(DriverExclusionKeyword)
            candidate.AddKeyword(DriverExclusionKeyword)
            AddedDriverExclusion = candidate.HasKeyword(DriverExclusionKeyword)
        endif
    endif
EndFunction

Function ObserveConvoyAutomation()
    ObserveDriverPoseExit()
    if CopilotHandPoseActor && !CopilotWeaponProbeBusy && CopilotHandPoseActor.Is3DLoaded()
        RestoreCopilotHandPose()
    endif
    if ConvoyStarting || FlightTestPreparing
        return
    endif
    ObjectReference playerVehicle = Game.GetPlayer().GetFurnitureUsing()
    int playerSitState = Game.GetPlayer().GetSitState()
    bool driving = playerVehicle && playerSitState == 3 && CassiopeiaPapyrusExtender.HasVehicleKeyword(playerVehicle)
    bool justExited = !driving && LastPlayerDriving
    ObjectReference departedVehicle = LastPlayerVehicle
    bool justEntered = driving && (!LastPlayerDriving || LastPlayerVehicle != playerVehicle)
    LastPlayerDriving = driving
    if Utility.GetCurrentRealTime() - LastConvoyDiagnostic >= 5.0 || justEntered
        LastConvoyDiagnostic = Utility.GetCurrentRealTime()
        WriteLog("CONVOY poll playerVehicle=" + playerVehicle + " sitState=" + playerSitState + " driving=" + driving + " justEntered=" + justEntered + " settings=" + Settings + " automatic=" + (Settings && Settings.AutoConvoyEnabled) + " assigned=" + AssignedDriver + " reserved=" + ReservedDriver + " activeDriver=" + ProbeDriver + " extraCount=" + ExtraVehicles.Length)
    endif
    if justExited && Settings && Settings.ExitWithPlayerEnabled
        ReleaseLeadPassengers(departedVehicle)
        if ConvoySession
            EndConvoy(false)
        endif
        LastPlayerVehicle = playerVehicle
        return
    endif
    if ConvoySession
        if driving && ConvoyPassengerFillPending
            FillConvoyPassengers(playerVehicle)
        endif
        ; Keep the order and seat hold when the player exits. Native lead validation sends neutral input.
        LastPlayerVehicle = playerVehicle
        return
    endif
    if ConvoyOrderRequested
        if DriverTransferPending
            if EligibleConvoyDriver(ConvoyOrderDriver)
                DriverTransferPending = false
            elseif Utility.GetCurrentRealTime() - DriverTransferStart > 20.0
                DriverTransferPending = false
                ConvoyOrderRequested = false
                AutoConvoySuppressed = true
                ClearDriverReservation()
                Debug.Notification("Driver could not leave the current seat. Convoy order cancelled.")
                return
            else
                return
            endif
        endif
        if !driving && EligibleConvoyDriver(ConvoyOrderDriver)
            ReserveConvoyDriver(ConvoyOrderDriver)
        endif
        if driving && Utility.GetCurrentRealTime() - LastConvoyResumeAttempt >= 1.0
            LastConvoyResumeAttempt = Utility.GetCurrentRealTime()
            if ConvoyOrderVehicle && ConvoyOrderVehicle.Is3DLoaded() && EligibleConvoyDriver(ConvoyOrderDriver)
                BeginConvoy(ConvoyOrderDriver, ConvoyOrderVehicle)
            endif
        endif
        LastPlayerVehicle = playerVehicle
        return
    endif
    if !Settings || ProbeDriver
        ClearDriverReservation()
        LastPlayerVehicle = playerVehicle
        return
    endif
    ObjectReference extra = AvailableConvoyVehicle()
    if !extra
        ClearDriverReservation()
        LastPlayerVehicle = playerVehicle
        if justEntered && Settings.AutoBoardEnabled
            FillLeadRearSeats(playerVehicle)
        endif
        return
    endif
    if !Settings.AutoConvoyEnabled || AutoConvoySuppressed
        if justEntered && Settings.AutoBoardEnabled
            FillLeadRearSeats(playerVehicle)
        endif
        if AssignedDriver && !driving
            ReserveConvoyDriver(ChooseConvoyDriver())
        elseif !AssignedDriver
            ClearDriverReservation()
        endif
        LastPlayerVehicle = playerVehicle
        return
    endif
    if !driving
        ReserveConvoyDriver(ChooseConvoyDriver())
    elseif driving
        Actor candidate = ReservedDriver
        if !EligibleConvoyDriver(candidate)
            candidate = ChooseConvoyDriver()
        endif
        if candidate
            BeginConvoy(candidate, extra)
        else
            ClearDriverReservation()
            WriteLog("CONVOY auto: no available driver; normal player vehicle boarding retained")
        endif
    endif
    LastPlayerVehicle = playerVehicle
EndFunction

Function BeginConvoy(Actor candidate, ObjectReference extra, bool parkedFlightTest = false)
    WriteLog("CONVOY begin requested driver=" + candidate + " extra=" + extra + " starting=" + ConvoyStarting + " existingDriver=" + ProbeDriver + " eligible=" + EligibleConvoyDriver(candidate))
    if ConvoyStarting || ProbeDriver || !EligibleConvoyDriver(candidate) || !extra
        return
    endif
    ObjectReference playerVehicle = Game.GetPlayer().GetFurnitureUsing()
    bool playerDriving = playerVehicle && Game.GetPlayer().GetSitState() == 3 && CassiopeiaPapyrusExtender.HasVehicleKeyword(playerVehicle)
    if playerVehicle == extra || (!playerDriving && !(parkedFlightTest && FlightTestPreparing && Game.GetPlayer().GetSitState() == 0))
        WriteLog("CONVOY boarding deferred: player is not driving the lead")
        return
    endif
    ConvoyOrderRequested = true
    ConvoyOrderDriver = candidate
    ConvoyOrderVehicle = extra
    ConvoyStarting = true
    ConvoyPassengers = new Actor[0]
    Actor previousFollower = SelectedFollower
    ObjectReference previousVehicle = SelectedVehicle
    ReserveConvoyDriver(candidate)
    SelectedFollower = candidate
    SelectedVehicle = extra
    StartDriverProbe(true)
    ConvoyPassengerFillPending = ConvoySession && ProbeDriver
    if !ConvoySession
        ClearDriverReservation()
    endif
    SelectedFollower = previousFollower
    SelectedVehicle = previousVehicle
    ConvoyStarting = false
    if playerDriving && ConvoyPassengerFillPending
        FillConvoyPassengers(playerVehicle)
    endif
EndFunction

Function FillConvoyPassengers(ObjectReference playerVehicle)
    if !ConvoySession || !ProbeDriver || !ProbeVehicle || !ConvoyPassengerFillPending
        return
    endif
    ConvoyPassengerFillPending = false
    Utility.Wait(0.35)
    if !ConvoySession || Game.GetPlayer().GetFurnitureUsing() != playerVehicle || Game.GetPlayer().GetSitState() != 3
        ConvoyPassengerFillPending = ConvoySession
        return
    endif
    Actor previousFollower = SelectedFollower
    ObjectReference previousVehicle = SelectedVehicle
    SelectedVehicle = ProbeVehicle
        int wanted = 0
        if Settings && Settings.AutoBoardEnabled && Settings.ConvoyPassengerLayout == 1
            wanted = 1
        elseif Settings && Settings.AutoBoardEnabled && Settings.ConvoyPassengerLayout == 2
            wanted = FreeHumanPassengerSeats(ProbeVehicle)
        endif
        Actor[] followers = ReadFollowers()
        WriteLog("CONVOY passenger plan layout=" + Settings.ConvoyPassengerLayout + " wanted=" + wanted + " followerCount=" + followers.Length)
        int index = 0
        int boarded = 0
        while followers && index < followers.Length && boarded < wanted
            Actor passenger = followers[index]
            WriteLog("CONVOY passenger ProbeDriver=" + passenger + " selectedDriver=" + (passenger == ProbeDriver) + " eligible=" + EligibleConvoyDriver(passenger) + " sitState=" + passenger.GetSitState() + " furniture=" + passenger.GetFurnitureUsing())
            if passenger != ProbeDriver && EligibleConvoyDriver(passenger)
                SelectedFollower = passenger
                BoardSelectedFollower()
                if OrderedActors.Find(passenger) >= 0
                    ConvoyPassengers.Add(passenger)
                    boarded += 1
                endif
                Utility.Wait(0.25)
            endif
            index += 1
        endwhile
        WriteLog("CONVOY begin driver=" + ProbeDriver + " lead=" + playerVehicle + " ProbeVehicle=" + ProbeVehicle + " additionalPassengers=" + boarded)
    SelectedFollower = previousFollower
    SelectedVehicle = previousVehicle
    if Settings && Settings.AutoBoardEnabled
        FillLeadRearSeats(playerVehicle)
    endif
EndFunction

Function UpdateConvoyLights(Actor candidate)
    if !candidate || !SyncLightsKeyword
        return
    endif
    bool desired = Settings && Settings.SyncConvoyLightsEnabled
    if desired && !AddedLightsKeyword && !candidate.HasKeyword(SyncLightsKeyword)
        candidate.AddKeyword(SyncLightsKeyword)
        AddedLightsKeyword = candidate.HasKeyword(SyncLightsKeyword)
    elseif !desired && AddedLightsKeyword
        candidate.RemoveKeyword(SyncLightsKeyword)
        AddedLightsKeyword = false
    endif
    if desired && AddedLightsKeyword
        candidate.AddKeyword(SyncLightsKeyword)
    endif
    bool enabled = candidate.HasKeyword(SyncLightsKeyword)
EndFunction

Function ClearConvoyKeywords(Actor candidate)
    ClearConvoyActionPolicies(candidate)
    if candidate && AddedLightsKeyword && SyncLightsKeyword
        candidate.RemoveKeyword(SyncLightsKeyword)
        bool lightsCleared = candidate.HasKeyword(SyncLightsKeyword)
    endif
    if candidate && AddedConvoyKeyword && ConvoyKeyword
        candidate.RemoveKeyword(ConvoyKeyword)
        bool convoyCleared = candidate.HasKeyword(ConvoyKeyword)
    endif
    AddedLightsKeyword = false
    AddedConvoyKeyword = false
EndFunction

Function EndConvoy(bool cancelOrder = true)
    if cancelOrder && LoadRecoveryPending
        RecoveryStopRequested = true
        return
    endif
    if cancelOrder && !RecoveryStopRequested
        RecoveryStopRequested = true
        LoadRecoveryPending = true
        LoadRecoveryAttempts = 0
        RecoverSavedOccupants()
        return
    endif
    RecoveryStopRequested = false
    if cancelOrder
        DriverTransferPending = false
        ConvoyPassengerFillPending = false
        AutoConvoySuppressed = true
        ConvoyOrderRequested = false
        ConvoyOrderDriver = None
        ConvoyOrderVehicle = None
    endif
    DeferDriverPoseRestore()
    Actor driver = ProbeDriver
    ObjectReference extra = ProbeVehicle
    ConvoySession = false
    ClearConvoyKeywords(driver)
    if driver && ProbeDriverKeyword && ProbeAddedKeyword
        driver.RemoveKeyword(ProbeDriverKeyword)
        driver.EvaluatePackage()
    endif
    ProbeAddedKeyword = false
    ProbeDriver = None
    ProbeVehicle = None
    ProbeDriverKeyword = None
    CancelTimer(115)
    ClearDriverReservation()
    Actor previousFollower = SelectedFollower
    int slot = 0
    while OrderedActors && slot < OrderedActors.Length
        if OrderedActors[slot] && OrderedVehicles[slot] == extra && ConvoyPassengers && ConvoyPassengers.Find(OrderedActors[slot]) >= 0
            SelectedFollower = OrderedActors[slot]
            DisembarkSelectedFollower()
            Utility.Wait(0.5)
        endif
        slot += 1
    endwhile
    SelectedFollower = previousFollower
    ConvoyPassengers = new Actor[0]
    WriteLog("CONVOY end driver=" + driver + " extra=" + extra + " cancelOrder=" + cancelOrder)
    if cancelOrder
        Debug.Notification("Convoy stopped. Followers released.")
    else
        Debug.Notification("Convoy paused. Your order is retained.")
    endif
EndFunction

Function OpenConvoyActions()
    Message menu = RV8VehicleFleet.GetModForm(0x80D) as Message
    EnsureConvoyRuntime()
    if !menu || !Settings
        WriteLog("CONVOY MENU unavailable menu=" + menu + " settings=" + Settings)
        Debug.Notification("Convoy controls could not initialize. Check the fleet log.")
        return
    endif
    int choice = menu.Show()
    WriteLog("CONVOY MENU choice=" + choice + " follower=" + SelectedFollower + " vehicle=" + SelectedVehicle)
    if choice == 0
        RequestManualConvoy()
    elseif choice == 1
        StopDriverOrder()
    elseif choice == 2
        CycleRequestedDriver()
    elseif choice == 3
        AssignedDriver = None
        ClearDriverReservation()
        Debug.Notification("Convoy driver selection: automatic.")
    elseif choice == 7
        RunBoostFlightTest()
    elseif choice == 6
        ObjectReference lead = Game.GetPlayer().GetFurnitureUsing()
        if !lead || !SupportedConvoyVehicle(lead)
            lead = GetActiveVehicle()
        endif
        ReleaseLeadPassengers(lead)
    elseif choice == 5
        AutoConvoySuppressed = !AutoConvoySuppressed
        if AutoConvoySuppressed
            ClearDriverReservation()
            Debug.Notification("Automatic convoy paused. Existing orders remain active.")
        else
            LastPlayerDriving = false
            Debug.Notification("Automatic convoy allowed. Gameplay Settings still apply.")
        endif
    elseif choice == 4
        Debug.MessageBox("Automatic convoy setting: " + Settings.AutoConvoyEnabled + "\nController pause: " + AutoConvoySuppressed + "\nExit with player: " + Settings.ExitWithPlayerEnabled + "\nPassenger layout: " + Settings.ConvoyPassengerLayout + " (0: driver only, 1: 2 + 2, 2: fill additional seats)\nSynchronize lights: " + Settings.SyncConvoyLightsEnabled + "\nConvoy running: " + ConvoySession)
    endif
EndFunction


Function FillLeadRearSeats(ObjectReference lead)
    if !lead || !SupportedConvoyVehicle(lead) || BoardingGroupBusy
        return
    endif
    BoardingGroupBusy = true
    ; Let the native front companion finish its boarding before assigning free rear seats.
    Utility.Wait(0.35)
    if Game.GetPlayer().GetFurnitureUsing() != lead || Game.GetPlayer().GetSitState() != 3
        BoardingGroupBusy = false
        return
    endif
    Actor previousFollower = SelectedFollower
    ObjectReference previousVehicle = SelectedVehicle
    SelectedVehicle = lead
    Actor[] followers = ReadFollowers()
    int index = 0
    while followers && index < followers.Length
        if EligibleConvoyDriver(followers[index]) && !followers[index].HasKeyword(CachedPrimarySeat)
            SelectedFollower = followers[index]
            BoardSelectedFollower()
            Utility.Wait(0.25)
        endif
        index += 1
    endwhile
    SelectedFollower = previousFollower
    SelectedVehicle = previousVehicle
    BoardingGroupBusy = false
    WriteLog("AUTO BOARD lead rear seats assigned; no additional vehicle available")
EndFunction


Function EnsureConvoyRuntime()
    if !Settings
        Settings = RV8VehicleFleet.GetModForm(0x804) as RV8VehicleSettings
    endif
    if Settings
        Settings.InitializeSettings()
    endif
    EnsureOrderStorage()
    RefreshPassengerForms()
    if !ConvoyKeyword
        ConvoyKeyword = RV8VehicleFleet.GetModForm(0x80B) as Keyword
        SyncLightsKeyword = RV8VehicleFleet.GetModForm(0x80C) as Keyword
        DriverExclusionKeyword = Game.GetFormFromFile(0x3197BE, "Starfield.esm") as Keyword
    endif
    StartTimer(0.5, 201)
    WriteLog("CONVOY runtime settings=" + Settings + " convoyKeyword=" + ConvoyKeyword + " lightsKeyword=" + SyncLightsKeyword + " roleBasedVehicles=true")
EndFunction


Function UpdatePlayerAcceleration()
    if !Settings
        return
    endif
    if !GradualAccelerationKeyword
        GradualAccelerationKeyword = RV8VehicleFleet.GetModForm(0x811) as Keyword
        ThrottleReleaseKeyword = RV8VehicleFleet.GetModForm(0x817) as Keyword
        AccelerationKeywords = new Keyword[5]
        int index = 0
        while index < 5
            AccelerationKeywords[index] = RV8VehicleFleet.GetModForm(0x812 + index) as Keyword
            index += 1
        endwhile
    endif
    if !GradualAccelerationKeyword || !ThrottleReleaseKeyword
        return
    endif
    Actor player = Game.GetPlayer()
    if Settings.GradualAccelerationEnabled
        if !OwnAccelerationTag && player.HasKeyword(GradualAccelerationKeyword)
            return
        endif
        player.AddKeyword(GradualAccelerationKeyword)
        OwnAccelerationTag = true
        int chosen = Settings.AccelerationTime - 1
        if chosen < 0 || chosen > 4
            chosen = 1
        endif
        if OwnedAccelerationTime != chosen && OwnedAccelerationTime >= 0
            player.RemoveKeyword(AccelerationKeywords[OwnedAccelerationTime])
            OwnedAccelerationTime = -1
        endif
        if AccelerationKeywords[chosen]
            if OwnedAccelerationTime == chosen || !player.HasKeyword(AccelerationKeywords[chosen])
                player.AddKeyword(AccelerationKeywords[chosen])
                OwnedAccelerationTime = chosen
            endif
        endif
        if Settings.GradualReleaseEnabled
            if OwnReleaseTag || !player.HasKeyword(ThrottleReleaseKeyword)
                player.AddKeyword(ThrottleReleaseKeyword)
                OwnReleaseTag = true
            endif
        elseif OwnReleaseTag
            player.RemoveKeyword(ThrottleReleaseKeyword)
            OwnReleaseTag = false
        endif
    else
        if OwnAccelerationTag
            player.RemoveKeyword(GradualAccelerationKeyword)
            OwnAccelerationTag = false
        endif
        if OwnedAccelerationTime >= 0
            player.RemoveKeyword(AccelerationKeywords[OwnedAccelerationTime])
            OwnedAccelerationTime = -1
        endif
        if OwnReleaseTag
            player.RemoveKeyword(ThrottleReleaseKeyword)
            OwnReleaseTag = false
        endif
    endif
EndFunction

; Hidden research entry point: no live menu changes or autonomous shooting.
Function CopilotWeaponProbe() Global
    RV8VehicleFleet fleet = RV8VehicleFleet.GetModForm(0x809) as RV8VehicleFleet
    if fleet
        fleet.RunCopilotWeaponProbe()
    endif
EndFunction

bool CopilotWeaponProbeBusy = false
Actor CopilotHandPoseActor
int CopilotHandPosePrevious = 0
bool CopilotHandPoseOwned = false

Function RestoreCopilotHandPose()
    Actor previous = CopilotHandPoseActor
    if !previous
        return
    endif
    if !previous.Is3DLoaded()
        WriteLog("COPILOT HAND restore deferred: actor is not loaded")
        return
    endif
    int original = CopilotHandPosePrevious
    bool owned = CopilotHandPoseOwned
    CopilotHandPoseActor = None
    CopilotHandPoseOwned = false
    if owned && previous.GetAnimationVariableInt("bEnableHandIK") == 1
        previous.SetAnimationVariableInt("bEnableHandIK", original)
        WriteLog("COPILOT HAND restore actor=" + previous + " original=" + original + " readback=" + previous.GetAnimationVariableInt("bEnableHandIK"))
    else
        WriteLog("COPILOT HAND restore skipped: owned value no longer active")
    endif
EndFunction

Function LogCopilotWeaponState(Actor candidate, ObjectReference vehicle, string phase)
    if !candidate || !candidate.Is3DLoaded()
        WriteLog("COPILOT WEAPON " + phase + " actor unloaded; graph sample skipped")
        return
    endif
    WriteLog("COPILOT WEAPON " + phase + " actor=" + candidate + " vehicle=" + vehicle + " furniture=" + candidate.GetFurnitureUsing() + " state=" + candidate.GetSitState() + " drawn=" + candidate.IsWeaponDrawn() + " weapon=" + candidate.GetEquippedWeapon() + " iIsPlayer=" + candidate.GetAnimationVariableInt("iIsPlayer") + " noAim=" + candidate.GetAnimationVariableBool("bNoAim") + " aimActive=" + candidate.GetAnimationVariableBool("bAimActive") + " boneWeights=" + candidate.GetAnimationVariableFloat("BoneWeightsAction") + " enableHandIK=" + candidate.GetAnimationVariableInt("bEnableHandIK") + " leftHandDisabled=" + candidate.GetAnimationVariableInt("bNoLeftHandIK") + " rightHandDisabled=" + candidate.GetAnimationVariableInt("bNoRightHandIK") + " aimHeading=" + candidate.GetAnimationVariableFloat("DirectAtHeadingVar") + " aimPitch=" + candidate.GetAnimationVariableFloat("DirectAtPitchVar"))
EndFunction

Function RunCopilotWeaponProbe()
    if CopilotWeaponProbeBusy
        return
    endif
    RestoreCopilotHandPose()
    if CopilotHandPoseActor
        Debug.Notification("Passenger animation cleanup is waiting for the actor to load.")
        return
    endif
    Actor candidate = SelectedFollower
    ObjectReference vehicle = None
    if candidate
        vehicle = candidate.GetFurnitureUsing()
    endif
    if CopilotWeaponProbeBusy || !candidate || candidate == Game.GetPlayer() || candidate == ProbeDriver || !vehicle || !candidate.Is3DLoaded() || !vehicle.Is3DLoaded() || candidate.GetSitState() != 3 || candidate.IsDead() || candidate.IsInCombat() || !CassiopeiaPapyrusExtender.HasVehicleKeyword(vehicle)
        Debug.Notification("Select a seated passenger before starting the weapon check.")
        return
    endif
    if CassiopeiaPapyrusExtender.GetFurnitureMarkerUser(vehicle, 0) == candidate
        Debug.Notification("The driver seat is excluded from the passenger weapon check.")
        return
    endif
    if !candidate.GetEquippedWeapon()
        Debug.Notification("The selected passenger has no equipped weapon.")
        return
    endif
    CopilotWeaponProbeBusy = true
    bool originallyDrawn = candidate.IsWeaponDrawn()
    LogCopilotWeaponState(candidate, vehicle, "before")
    CopilotHandPosePrevious = candidate.GetAnimationVariableInt("bEnableHandIK")
    if CopilotHandPosePrevious == 0
        CopilotHandPoseActor = candidate
        CopilotHandPoseOwned = true
        ; Use the graph's normal transition. Do not zero its nested IK counters.
        candidate.SetAnimationVariableInt("bEnableHandIK", 1)
    endif
    Debug.Notification("Passenger hand animation check: watch for 8 seconds.")
    candidate.DrawWeapon()
    float started = Utility.GetCurrentRealTime()
    float nextSample = started + 1.0
    while (Utility.GetCurrentRealTime() - started) < 8.0 && candidate.Is3DLoaded() && vehicle.Is3DLoaded() && candidate.GetFurnitureUsing() == vehicle && candidate.GetSitState() == 3 && !candidate.IsInCombat() && !candidate.IsDead()
        Utility.Wait(0.5)
        if Utility.GetCurrentRealTime() >= nextSample && candidate.Is3DLoaded() && candidate.GetFurnitureUsing() == vehicle && candidate.GetSitState() == 3
            LogCopilotWeaponState(candidate, vehicle, "hand-enabled")
            nextSample = Utility.GetCurrentRealTime() + 3.0
        endif
    endwhile
    RestoreCopilotHandPose()
    if !originallyDrawn && candidate.Is3DLoaded() && !candidate.IsInCombat() && candidate.IsWeaponDrawn()
        candidate.SheatheWeapon()
    endif
    LogCopilotWeaponState(candidate, vehicle, "after")
    CopilotWeaponProbeBusy = false
    Debug.Notification("Passenger weapon check finished.")
EndFunction

bool VehicleActionProbeBusy = false
Keyword VerticalRecoveryKeyword
Keyword ForwardSupportKeyword
bool OwnedVerticalRecovery = false
bool OwnedForwardSupport = false
float LastMountedSupportRequest = 0.0
Actor ObservedSupportEnemy
float ObservedSupportHealth = 0.0
Actor RecentSupportAttacker
float RecentSupportAttackTime = 0.0
bool SupportIncomingHitsOwned = false
float LastSupportBlockLog = 0.0

Function VehicleActionProbe() Global
    RV8VehicleFleet fleet = RV8VehicleFleet.GetModForm(0x809) as RV8VehicleFleet
    if fleet
        fleet.RunVehicleActionProbe()
    endif
EndFunction

Function RunVehicleActionProbe()
    Actor driver = ProbeDriver
    ObjectReference vehicle = ProbeVehicle
    if VehicleActionProbeBusy || !ConvoySession || !driver || !vehicle || !driver.Is3DLoaded() || !vehicle.Is3DLoaded() || driver.GetSitState() != 3 || driver.GetFurnitureUsing() != vehicle || driver.IsInCombat() || Game.GetPlayer().IsInCombat() || Game.GetPlayer().GetSitState() != 0
        Debug.Notification("Park the convoy, dismount and leave its driver seated before running the action test.")
        return
    endif
    Keyword mounted = RV8VehicleFleet.GetModForm(0x81B) as Keyword
    Keyword vertical = RV8VehicleFleet.GetModForm(0x81C) as Keyword
    if !mounted || !vertical || driver.HasKeyword(mounted) || driver.HasKeyword(vertical)
        Debug.Notification("Vehicle action test is unavailable.")
        return
    endif
    VehicleActionProbeBusy = true
    WriteLog("ACTION TEST begin driver=" + driver + " vehicle=" + vehicle + " weapon then vertical")
    driver.AddKeyword(mounted)
    WriteLog("ACTION TEST mounted request=" + driver.HasKeyword(mounted))
    Utility.Wait(2.0)
    driver.RemoveKeyword(mounted)
    if ConvoySession && ProbeDriver == driver && driver.GetSitState() == 3 && !driver.IsInCombat() && Game.GetPlayer().GetSitState() == 0
        WriteLog("ACTION TEST vertical before z=" + vehicle.GetPositionZ())
        driver.AddKeyword(vertical)
        WriteLog("ACTION TEST vertical request=" + driver.HasKeyword(vertical))
        Utility.Wait(3.0)
        driver.RemoveKeyword(vertical)
        WriteLog("ACTION TEST vertical after z=" + vehicle.GetPositionZ())
    endif
    VehicleActionProbeBusy = false
    WriteLog("ACTION TEST completed driver=" + driver + " furniture=" + driver.GetFurnitureUsing())
    Debug.Notification("Vehicle action test finished. Check mounted fire and vertical movement.")
EndFunction

Function ClearConvoyActionPolicies(Actor driver)
    ReleaseSupportTarget()
    if SupportIncomingHitsOwned
        UnregisterForAllHitEvents(Game.GetPlayer())
        if driver
            UnregisterForAllHitEvents(driver)
        endif
    endif
    SupportIncomingHitsOwned = false
    RecentSupportAttacker = None
    if driver && OwnedVerticalRecovery && VerticalRecoveryKeyword
        driver.RemoveKeyword(VerticalRecoveryKeyword)
    endif
    if driver && OwnedForwardSupport && ForwardSupportKeyword
        driver.RemoveKeyword(ForwardSupportKeyword)
    endif
    OwnedVerticalRecovery = false
    OwnedForwardSupport = false
EndFunction

Function ClearForwardSupport(Actor driver)
    if driver && OwnedForwardSupport && ForwardSupportKeyword
        driver.RemoveKeyword(ForwardSupportKeyword)
    endif
    OwnedForwardSupport = false
EndFunction

Function UpdateConvoyActionPolicies(Actor driver)
    ObserveSupportHealth()
    if !driver || !Settings || !ProbeVehicle || !driver.Is3DLoaded() || driver.GetSitState() != 3 || driver.GetFurnitureUsing() != ProbeVehicle
        ClearConvoyActionPolicies(driver)
        return
    endif
    if !VerticalRecoveryKeyword
        VerticalRecoveryKeyword = RV8VehicleFleet.GetModForm(0x81D) as Keyword
        ForwardSupportKeyword = RV8VehicleFleet.GetModForm(0x81E) as Keyword
    endif
    Cell vehicleCell = ProbeVehicle.GetParentCell()
    bool verticalAllowed = Settings.VerticalRecoveryEnabled && vehicleCell && !vehicleCell.IsInterior() && !driver.IsInCombat()
    if verticalAllowed && VerticalRecoveryKeyword
        if !OwnedVerticalRecovery && !driver.HasKeyword(VerticalRecoveryKeyword)
            driver.AddKeyword(VerticalRecoveryKeyword)
            OwnedVerticalRecovery = driver.HasKeyword(VerticalRecoveryKeyword)
        elseif OwnedVerticalRecovery
            driver.AddKeyword(VerticalRecoveryKeyword)
        endif
    elseif OwnedVerticalRecovery && VerticalRecoveryKeyword
        driver.RemoveKeyword(VerticalRecoveryKeyword)
        OwnedVerticalRecovery = false
    endif
    if !Settings.MountedSupportEnabled || !ForwardSupportKeyword || VehicleActionProbeBusy
        ClearForwardSupport(driver)
        ReleaseSupportTarget()
        if SupportIncomingHitsOwned
            UnregisterForAllHitEvents(Game.GetPlayer())
            UnregisterForAllHitEvents(driver)
            SupportIncomingHitsOwned = false
        endif
        return
    endif
    RegisterForHitEvent(Game.GetPlayer())
    RegisterForHitEvent(driver)
    SupportIncomingHitsOwned = true
    if !driver.IsInCombat() && !Game.GetPlayer().IsInCombat()
        BlockMountedSupport(driver, "not in combat")
        return
    endif
    Actor enemy = driver.GetCombatTarget()
    if !enemy
        enemy = Game.GetPlayer().GetCombatTarget()
    endif
    if !enemy && RecentSupportAttacker && Utility.GetCurrentRealTime() - RecentSupportAttackTime < 6.0
        enemy = RecentSupportAttacker
    endif
    if !enemy
        enemy = CassiopeiaPapyrusExtender.GetCrosshairRef() as Actor
    endif
    if !enemy || enemy == Game.GetPlayer() || enemy == driver || enemy.IsDead() || !enemy.Is3DLoaded()
        BlockMountedSupport(driver, "no live target", enemy)
        return
    endif
    if !enemy.IsHostileToActor(Game.GetPlayer()) && !enemy.IsHostileToActor(driver)
        BlockMountedSupport(driver, "target is not hostile", enemy)
        return
    endif
    float range = ProbeVehicle.GetDistance(enemy)
    float heading = ProbeVehicle.GetHeadingAngle(enemy)
    float height = Math.Abs(enemy.GetPositionZ() - ProbeVehicle.GetPositionZ())
    if range < 10.0 || range > 70.0 || Math.Abs(heading) > 4.0 || height > 1.5 || !driver.HasDetectionLOS(enemy)
        BlockMountedSupport(driver, "range=" + range + " heading=" + heading + " height=" + height + " LOS=" + driver.HasDetectionLOS(enemy), enemy)
        return
    endif
    Actor player = Game.GetPlayer()
    if ProbeVehicle.GetDistance(player) < range && Math.Abs(ProbeVehicle.GetHeadingAngle(player) - heading) < 12.0
        BlockMountedSupport(driver, "player ahead of target", enemy)
        return
    endif
    ObjectReference lead = player.GetFurnitureUsing()
    if lead && lead != ProbeVehicle && ProbeVehicle.GetDistance(lead) < range && Math.Abs(ProbeVehicle.GetHeadingAngle(lead) - heading) < 12.0
        BlockMountedSupport(driver, "lead vehicle ahead of target", enemy)
        return
    endif
    Actor[] friends = ReadFollowers()
    int index = 0
    while friends && index < friends.Length && index < 32
        Actor friend = friends[index]
        if friend && friend != driver && friend != enemy && friend.Is3DLoaded() && friend.GetFurnitureUsing() != ProbeVehicle
            if ProbeVehicle.GetDistance(friend) < range && Math.Abs(ProbeVehicle.GetHeadingAngle(friend) - heading) < 12.0
                BlockMountedSupport(driver, "follower ahead of target: " + friend, enemy)
                return
            endif
        endif
        index += 1
    endwhile
    float now = Utility.GetCurrentRealTime()
    if now - LastMountedSupportRequest < 1.5
        return
    endif
    ClearForwardSupport(driver)
    if !driver.HasKeyword(ForwardSupportKeyword)
        TrackSupportTarget(enemy)
        driver.AddKeyword(ForwardSupportKeyword)
        OwnedForwardSupport = driver.HasKeyword(ForwardSupportKeyword)
        LastMountedSupportRequest = now
        WriteLog("MOUNTED SUPPORT requested target=" + enemy + " distance=" + range + " heading=" + heading + " detectionLOS=true health=" + enemy.GetValue(Game.GetHealthAV()))
    endif
EndFunction

Function BlockMountedSupport(Actor driver, string reason, Actor enemy = None)
    ClearForwardSupport(driver)
    ReleaseSupportTarget()
    float now = Utility.GetCurrentRealTime()
    if now - LastSupportBlockLog >= 5.0
        LastSupportBlockLog = now
        WriteLog("MOUNTED SUPPORT blocked reason=" + reason + " target=" + enemy)
    endif
EndFunction

Function ObserveSupportHealth()
    if ObservedSupportEnemy && ObservedSupportEnemy.Is3DLoaded()
        float health = ObservedSupportEnemy.GetValue(Game.GetHealthAV())
        if health != ObservedSupportHealth
            WriteLog("MOUNTED HEALTH target=" + ObservedSupportEnemy + " before=" + ObservedSupportHealth + " after=" + health + " change source not isolated")
            ObservedSupportHealth = health
        endif
    endif
EndFunction

Function ReleaseSupportTarget()
    if ObservedSupportEnemy
        ObserveSupportHealth()
        UnregisterForAllHitEvents(ObservedSupportEnemy)
    endif
    ObservedSupportEnemy = None
EndFunction

Function TrackSupportTarget(Actor enemy)
    if ObservedSupportEnemy != enemy
        ReleaseSupportTarget()
        ObservedSupportEnemy = enemy
        ObservedSupportHealth = enemy.GetValue(Game.GetHealthAV())
        RegisterForHitEvent(enemy)
    endif
EndFunction

Event OnHit(ObjectReference akTarget, ObjectReference akAggressor, Form akSource, Projectile akProjectile, bool abPowerAttack, bool abSneakAttack, bool abBashAttack, bool abHitBlocked, string asMaterialName)
    if SupportIncomingHitsOwned && (akTarget == Game.GetPlayer() || akTarget == ProbeDriver)
        Actor attacker = akAggressor as Actor
        if attacker && attacker.IsHostileToActor(Game.GetPlayer())
            RecentSupportAttacker = attacker
            RecentSupportAttackTime = Utility.GetCurrentRealTime()
            WriteLog("MOUNTED SUPPORT recent hostile attacker=" + attacker)
        endif
        if ConvoySession && Settings && Settings.MountedSupportEnabled
            RegisterForHitEvent(akTarget)
        endif
    endif
    if akTarget == ObservedSupportEnemy
        WriteLog("MOUNTED HIT target=" + akTarget + " aggressor=" + akAggressor + " source=" + akSource + " projectile=" + akProjectile + " blocked=" + abHitBlocked + " driverMatch=" + (akAggressor == ProbeDriver) + " vehicleMatch=" + (akAggressor == ProbeVehicle) + " playerMatch=" + (akAggressor == Game.GetPlayer()))
        ObserveSupportHealth()
        if ConvoySession && Settings && Settings.MountedSupportEnabled && ObservedSupportEnemy && !ObservedSupportEnemy.IsDead()
            RegisterForHitEvent(ObservedSupportEnemy)
        endif
    endif
EndEvent

Function FinishBoostFlightTest(bool ownSession, Actor driver)
    FlightTestPreparing = false
    VehicleActionProbeBusy = false
    if ownSession && ConvoySession && ProbeDriver == driver
        EndConvoy(false)
    endif
EndFunction

Function RunBoostFlightTest()
    if VehicleActionProbeBusy || FlightTestPreparing
        Debug.Notification("A vehicle test is already running.")
        return
    endif
    if Game.GetPlayer().GetSitState() != 0 || Game.GetPlayer().IsInCombat()
        Debug.Notification("Stay on foot outside combat before starting the flight test.")
        return
    endif
    Actor driver = ProbeDriver
    ObjectReference vehicle = ProbeVehicle
    bool ownSession = false
    if !ConvoySession || !driver || !vehicle
        if ProbeDriver || ConvoyStarting
            Debug.Notification("Wait for the current driver order to finish.")
            return
        endif
        driver = ChooseRequestedDriver()
        vehicle = AvailableConvoyVehicle()
        if !EligibleConvoyDriver(driver) || !vehicle
            Debug.Notification("Deploy an empty additional vehicle and keep a human follower on foot first.")
            return
        endif
        ownSession = true
    endif
    Cell testCell = vehicle.GetParentCell()
    Keyword signal = RV8VehicleFleet.GetModForm(0x825) as Keyword
    if !driver || !vehicle || !driver.Is3DLoaded() || !vehicle.Is3DLoaded() || driver.IsDead() || driver.IsInCombat() || !testCell || testCell.IsInterior() || !signal || driver.HasKeyword(signal)
        Debug.Notification("Use an outdoor area with an available follower and additional vehicle.")
        return
    endif
    ; This explicit experiment may board its driver while the player is on foot.
    ; Normal Start convoy retains the player-driving gate and settings unchanged.
    VehicleActionProbeBusy = true
    FlightTestPreparing = true
    MenuBusy = false
    if ownSession
        Debug.Notification("Flight test: preparing the driver. Stay on foot; no settings changes are needed.")
        BeginConvoy(driver, vehicle, true)
    endif
    float prepared = Utility.GetCurrentRealTime()
    while ConvoySession && ProbeDriver == driver && driver.Is3DLoaded() && vehicle.Is3DLoaded() && (driver.GetSitState() != 3 || driver.GetFurnitureUsing() != vehicle) && Utility.GetCurrentRealTime() - prepared < 12.0
        if driver.IsDead() || driver.IsInCombat() || Game.GetPlayer().IsInCombat() || Game.GetPlayer().GetSitState() != 0
            FinishBoostFlightTest(ownSession, driver)
            return
        endif
        Utility.Wait(0.25)
    endwhile
    if !ConvoySession || ProbeDriver != driver || !driver.Is3DLoaded() || !vehicle.Is3DLoaded() || driver.GetSitState() != 3 || driver.GetFurnitureUsing() != vehicle
        FinishBoostFlightTest(ownSession, driver)
        Debug.Notification("Flight test could not prepare its driver. No boost was requested.")
        return
    endif
    driver.AddKeyword(signal)
    bool sent = driver.HasKeyword(signal)
    driver.RemoveKeyword(signal)
    WriteLog("BOOST FLIGHT self-contained request issued=" + sent + " ownSession=" + ownSession + " driver=" + driver + " vehicle=" + vehicle)
    Debug.Notification("Flight test running: stay on foot and watch for 20 seconds. Stop convoy cancels it.")
    float began = Utility.GetCurrentRealTime()
    while Utility.GetCurrentRealTime() - began < 20.5 && ConvoySession && ProbeDriver == driver && driver.Is3DLoaded() && vehicle.Is3DLoaded() && driver.GetFurnitureUsing() == vehicle && driver.GetSitState() == 3
        if driver.IsDead() || driver.IsInCombat() || Game.GetPlayer().IsInCombat() || Game.GetPlayer().GetSitState() != 0
            StopDriverOrder()
            FinishBoostFlightTest(ownSession, driver)
            return
        endif
        Utility.Wait(0.5)
    endwhile
    FinishBoostFlightTest(ownSession, driver)
    Debug.Notification("Flight observation finished. Landing is not automatically confirmed.")
EndFunction
