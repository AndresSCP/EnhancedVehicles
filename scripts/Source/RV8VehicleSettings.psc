Scriptname RV8VehicleSettings extends Quest
GameplayOption Property ExitWithPlayerOption Auto
bool Property ExitWithPlayerEnabled = true Auto Hidden
GameplayOption Property VerticalRecoveryOption Auto
GameplayOption Property MountedSupportOption Auto
bool Property VerticalRecoveryEnabled = false Auto Hidden
bool Property MountedSupportEnabled = false Auto Hidden

GameplayOption Property GradualAccelerationOption Auto
GameplayOption Property AccelerationTimeOption Auto
GameplayOption Property ThrottleReleaseOption Auto
bool Property GradualAccelerationEnabled = false Auto Hidden
int Property AccelerationTime = 2 Auto Hidden
bool Property GradualReleaseEnabled = false Auto Hidden
GameplayOption Property AutoConvoyOption Auto
GameplayOption Property ConvoyLayoutOption Auto
GameplayOption Property SyncLightsOption Auto
bool Property AutoConvoyEnabled = true Auto Hidden
int Property ConvoyPassengerLayout = 1 Auto Hidden
bool Property SyncConvoyLightsEnabled = true Auto Hidden
GameplayOption Property AutoBoardOption Auto
GameplayOption Property ExtraVehicleLimitOption Auto
GameplayOption Property StaggeredExitOption Auto

bool Property AutoBoardEnabled = true Auto Hidden
bool Property StaggeredExitEnabled = true Auto Hidden
int Property ExtraVehicleLimit = 1 Auto Hidden

Event OnQuestInit()
    InitializeSettings()
EndEvent

Function InitializeSettings()
    RegisterForGameplayOptionChangedEvent()
    RefreshSettings()
EndFunction

Function RefreshSettings()
    if !ExitWithPlayerOption
        ExitWithPlayerOption = RV8VehicleFleet.GetModForm(0x821) as GameplayOption
    endif
    if ExitWithPlayerOption
        ExitWithPlayerEnabled = ExitWithPlayerOption.GetValue() != 0.0
    endif
    if !VerticalRecoveryOption
        VerticalRecoveryOption = RV8VehicleFleet.GetModForm(0x81F) as GameplayOption
        MountedSupportOption = RV8VehicleFleet.GetModForm(0x820) as GameplayOption
    endif
    if VerticalRecoveryOption
        VerticalRecoveryEnabled = VerticalRecoveryOption.GetValue() != 0.0
    endif
    if MountedSupportOption
        MountedSupportEnabled = MountedSupportOption.GetValue() != 0.0
    endif
    if !GradualAccelerationOption
        GradualAccelerationOption = RV8VehicleFleet.GetModForm(0x818) as GameplayOption
        AccelerationTimeOption = RV8VehicleFleet.GetModForm(0x819) as GameplayOption
        ThrottleReleaseOption = RV8VehicleFleet.GetModForm(0x81A) as GameplayOption
    endif
    if GradualAccelerationOption
        GradualAccelerationEnabled = GradualAccelerationOption.GetValue() != 0.0
    endif
    if AccelerationTimeOption
        AccelerationTime = AccelerationTimeOption.GetValue() as int
    endif
    if ThrottleReleaseOption
        GradualReleaseEnabled = ThrottleReleaseOption.GetValue() != 0.0
    endif
    if !AutoConvoyOption
        AutoConvoyOption = RV8VehicleFleet.GetModForm(0x80E) as GameplayOption
        ConvoyLayoutOption = RV8VehicleFleet.GetModForm(0x80F) as GameplayOption
        SyncLightsOption = RV8VehicleFleet.GetModForm(0x810) as GameplayOption
    endif
    if AutoConvoyOption
        AutoConvoyEnabled = AutoConvoyOption.GetValue() != 0.0
    endif
    if ConvoyLayoutOption
        ConvoyPassengerLayout = ConvoyLayoutOption.GetValue() as int
    endif
    if SyncLightsOption
        SyncConvoyLightsEnabled = SyncLightsOption.GetValue() != 0.0
    endif
    if AutoBoardOption
        AutoBoardEnabled = AutoBoardOption.GetValue() != 0.0
    endif
    if StaggeredExitOption
        StaggeredExitEnabled = StaggeredExitOption.GetValue() != 0.0
    endif
    if ExtraVehicleLimitOption
        int requested = ExtraVehicleLimitOption.GetValue() as int
        if requested < 0
            requested = 0
        elseif requested > 4
            requested = 4
        endif
        ExtraVehicleLimit = requested
    endif
EndFunction

Event OnGameplayOptionChanged(GameplayOption[] aChangedOptions)
    ; Settings update only preferences; never despawn a vehicle on a menu change.
    RefreshSettings()
    GameplayOption.NotifyGameplayOptionUpdateFinished()
EndEvent
