class AndroidPortViewportClient extends UTGameViewportClient;

var transient UTUIFrontEnd_SettingsPanels PortPanels;
var transient UTUITabPage_Options PortPage;
var transient AndroidPortDataStore PortStore;
var transient AndroidPortValueStore PortValueStore;
var transient AndroidPortMenuScale PortMenuScale;
var transient float NextPanelCheck;
var transient bool PortPageReady;

// Read by the host through reflected IntProperty metadata on this viewport.
// Mode: 0 no controller/loading, 1 gameplay (including respawn), 2 capturing UI.
var transient int PortInputMode;
var transient int PortInputEpoch;
var transient int PortVehicleProfile;
// Host-to-script mailbox: any nonzero requests the Android settings page.
// Script acknowledges receipt with zero; no config persistence is involved.
var transient int PortMenuRequest;
var transient bool PortMenuPending;
var transient float NextMenuCheck;
var transient string PortRequestedPanelPath;
var transient bool PortStartupLoginHandled;
var transient bool PortStartupCDKeyHandled;
var transient float NextStartupLoginCheck;
var transient UIObject PortScrollTarget;
// -8..8 rows, or a pointer request: 64 begins pointing (each Tick the row
// under the cursor is highlighted), 65 taps the option row under the cursor.
var transient int PortScrollSteps;
// Weapon bar for the touch layer, read by the host without an input reset:
// bits 0-9 HUD slots in use, 10-13 the selected slot, 14 the bar is shown,
// 15 the HUD draws its minimap (UTHUD bHasMap and bShowMap).
var transient int PortWeaponSlots;
// Host mailbox, (phase << 8) | (slot + 1): 1 preview the slot, 2 switch to it.
var transient int PortWeaponRequest;
// The weapon whose card the original bar enlarges during a touch preview. The
// host shows it as PendingWeapon only inside the original DrawGameHUD call.
var transient Weapon PortWeaponPreview;
// Vehicle details which keep the input context: 1 Jump deploys/undeploys,
// 2 deployed, 4 a gunner seat passing Jump on, 8 look steering, 16 its
// stick deflection throttle, 32 moving backwards.
var transient int PortVehicleFlags;
// Console look steering (UTVehicle.SetInputs for UTVC_Simple): the steering
// toward the view and the vehicle's own reverse threshold and stick scale.
var transient float PortSteer;
var transient float PortSteerReverse;
var transient float PortSteerScale;
// Real time of the last list step made by a finger held beyond the list.
var transient float PortEdgeTime;
// A finger points at PortScrollTarget (request 64) until the host retires the
// gesture; the cursor it last saw, so rows follow only real movement.
var transient bool PortHovering;
var transient int PortHoverX, PortHoverY;
// The list a finger drags (request 64): where the finger began in the list's
// own pixels, the first visible row, the selection and the row under the
// finger then. The view follows the finger; the selection stays.
var transient UIObject PortDragTarget;
var transient float PortDragY;
var transient int PortDragTop, PortDragIndex, PortDragRow;
var transient bool PortDragMoved;
var transient float PortLastTapTime;
// An option list whose view a drag moved away from its focused option: the
// focused row keeps its lit label and the selection bar waits for the view.
var transient UTUIOptionList PortScrolledOptions;
// Checks of the shown settings page until its combo texts are refreshed (2).
var transient int PortComboWait;
// A press beside the settings tabs, on the tab it is nearest to; -1 none.
var transient int PortTabPress;
var transient bool PortTabsHooked;
// The mouse cursor drawn transparent, the skin it came from and its style.
var transient UITexture PortHiddenCursor;
var transient UISkin PortCursorSkin;
var transient UIStyle_Image PortCursorStyle;
// Paths do not keep an old world's objects alive through a map change.
var transient string PortWorldPath;
var transient string PortControllerPath;
var transient string PortPawnPath;
var transient string PortInputScenePath;
var transient float PortWorldSeconds;
var transient bool PortPawnAlive;

static function int ClassifyPortVehicle(class<Pawn> PawnClass)
{
    if (PawnClass == None)
        return 0;
    // Hover vehicles also include aircraft, so classify the board explicitly.
    if (ClassIsChildOf(PawnClass, class'UTVehicle_Hoverboard'))
        return 3;
    if (ClassIsChildOf(PawnClass, class'UTWeaponPawn')
        || ClassIsChildOf(PawnClass, class'UTVehicle_TrackTurretBase'))
        return 4;
    if (ClassIsChildOf(PawnClass, class'UTAirVehicle'))
        return 2;
    if (ClassIsChildOf(PawnClass, class'Vehicle'))
        return 1;
    return 0;
}

function PublishPortInputContext(WorldInfo CurrentWorld, PlayerController CurrentController,
    Pawn CurrentPawn, UIScene CurrentScene, bool bCapturingUI)
{
    local int Mode, Profile;
    local bool bAlive;
    local string WorldPath, ControllerPath, PawnPath, ScenePath;
    local float WorldSeconds;
    bAlive = CurrentPawn != None && !CurrentPawn.bDeleteMe && CurrentPawn.Health > 0;
    if (CurrentWorld != None)
    {
        WorldPath = PathName(CurrentWorld);
        WorldSeconds = CurrentWorld.TimeSeconds;
    }
    if (CurrentController != None)
        ControllerPath = PathName(CurrentController);
    if (CurrentPawn != None)
        PawnPath = PathName(CurrentPawn);
    if (CurrentScene != None)
        ScenePath = PathName(CurrentScene);
    if (bCapturingUI)
        Mode = 2;
    // Keep Fire/Start available for the original game's respawn handling.
    else if (CurrentController != None && !CurrentController.bDeleteMe)
        Mode = 1;
    if (bAlive)
        Profile = ClassifyPortVehicle(CurrentPawn.Class);
    if (WorldPath != PortWorldPath || ControllerPath != PortControllerPath
        || PawnPath != PortPawnPath || ScenePath != PortInputScenePath
        || Mode != PortInputMode || Profile != PortVehicleProfile || bAlive != PortPawnAlive)
    {
        // Publish the snapshot before its revision. The host reads under VM ownership.
        PortWorldPath = WorldPath;
        PortControllerPath = ControllerPath;
        PortPawnPath = PawnPath;
        PortInputScenePath = ScenePath;
        PortPawnAlive = bAlive;
        PortInputMode = Mode;
        PortVehicleProfile = Profile;
        PortInputEpoch = PortInputEpoch == 2147483647 ? 1 : PortInputEpoch + 1;
    }
    else if (WorldSeconds < PortWorldSeconds)
    {
        // Reloading the same map may reuse object paths but restarts world time.
        PortInputEpoch = PortInputEpoch == 2147483647 ? 1 : PortInputEpoch + 1;
    }
    PortWorldSeconds = WorldSeconds;
}

function UpdatePortInputContext()
{
    local WorldInfo CurrentWorld;
    local PlayerController CurrentController;
    local Pawn CurrentPawn;
    local UIScene CurrentScene;
    local bool bCapturingUI;
    local name ConsoleState;
    local int PreviousEpoch;
    PreviousEpoch = PortInputEpoch;
    CurrentWorld = GetCurrentWorldInfo();
    if (GamePlayers.Length > 0 && GamePlayers[0] != None)
    {
        CurrentController = GamePlayers[0].Actor;
        if (CurrentController != None && !CurrentController.bDeleteMe)
            CurrentPawn = CurrentController.Pawn;
    }
    if (UIController != None && UIController.SceneClient != None)
    {
        // UISceneClient's native-only SCENEFILTER_InputProcessorOnly is 0x2.
        // A non-interactive HUD scene must not hide gameplay controls.
        bCapturingUI = UIController.SceneClient.IsUIActive(2);
        if (bCapturingUI)
            CurrentScene = UIController.SceneClient.GetActiveScene();
    }
    // The original text console can capture input without an active UIScene.
    if (ViewportConsole != None)
    {
        ConsoleState = ViewportConsole.GetStateName();
        if (ConsoleState == 'Open' || ConsoleState == 'Typing')
            bCapturingUI = true;
    }
    PublishPortInputContext(CurrentWorld, CurrentController, CurrentPawn, CurrentScene, bCapturingUI);
    if (PreviousEpoch != PortInputEpoch)
    {
        PortScrollSteps = 0;
        PortScrollTarget = None;
        PortHovering = false;
        PortDragTarget = None;
        PortScrolledOptions = None;
        PortTabPress = -1;
        PortWeaponRequest = 0;
        PortWeaponPreview = None;
    }
    UpdatePortWeaponBar(CurrentController, CurrentPawn);
    HandlePortWeaponRequest(CurrentPawn);
    UpdatePortVehicle(CurrentPawn);
}

/** The weapon the original bar shows: pending, else held. */
static function UTWeapon PortSelectedWeapon(Pawn P)
{
    if (P == None || P.InvManager == None)
        return None;
    if (UTWeapon(P.InvManager.PendingWeapon) != None)
        return UTWeapon(P.InvManager.PendingWeapon);
    return UTWeapon(P.Weapon);
}

/** The weapon in a bar slot: like the original, the selected one, else the last of its group. */
static function UTWeapon PortWeaponInSlot(Pawn P, int Slot)
{
    local UTWeapon W, Found;
    if (P == None || P.InvManager == None || Slot < 0 || Slot > 9)
        return None;
    W = PortSelectedWeapon(P);
    if (W != None && W.InventoryGroup == Slot + 1)
        return W;
    foreach P.InvManager.InventoryActors(class'UTWeapon', W)
    {
        if (W.InventoryGroup == Slot + 1)
            Found = W;
    }
    return Found;
}

/** Mirrors the original native DisplayWeaponBar's visibility and slots. */
function UpdatePortWeaponBar(PlayerController CurrentController, Pawn CurrentPawn)
{
    local UTHUD Hud;
    local UTWeapon W, Selected;
    local int Slots, Count, Slot, Map;
    if (CurrentController != None)
        Hud = UTHUD(CurrentController.myHUD);
    if (Hud != None && Hud.bHasMap && Hud.bShowMap)
        Map = 1 << 15;
    if (PortInputMode == 1 && Hud != None && Hud.bShowHUD && Hud.bShowWeaponBar && CurrentPawn != None
        && Vehicle(CurrentPawn) == None && CurrentPawn.Health > 0 && CurrentPawn.InvManager != None
        && CurrentPawn.Weapon != None)
    {
        foreach CurrentPawn.InvManager.InventoryActors(class'UTWeapon', W)
        {
            if (W.InventoryGroup > 0 && W.InventoryGroup < 11)
            {
                Slots = Slots | (1 << (W.InventoryGroup - 1));
                ++Count;
            }
        }
        Selected = PortSelectedWeapon(CurrentPawn);
    }
    // The original draws nothing for fewer than two weapons.
    if (Count < 2 || Selected == None)
    {
        PortWeaponSlots = Map;
        PortWeaponPreview = None;
        return;
    }
    Slot = Clamp(Selected.InventoryGroup - 1, 0, 9);
    PortWeaponSlots = Slots | (1 << Slot) | (Slot << 10) | (1 << 14) | Map;
    if (PortWeaponPreview != None && (PortWeaponPreview.bDeleteMe
        || PortWeaponPreview.InvManager != CurrentPawn.InvManager || PortWeaponPreview == Selected))
        PortWeaponPreview = None;
}

/** A touch swipe previews a slot; its release switches through the original inventory. */
function HandlePortWeaponRequest(Pawn CurrentPawn)
{
    local int Request, Slot;
    local UTWeapon Target, Selected;
    local UTInventoryManager Inventory;
    Request = PortWeaponRequest;
    if (Request == 0)
        return;
    PortWeaponRequest = 0;
    Slot = (Request & 255) - 1;
    if ((PortWeaponSlots & (1 << 14)) == 0 || Slot < 0 || Slot > 9 || (PortWeaponSlots & (1 << Slot)) == 0
        || (Request >> 8) < 1 || (Request >> 8) > 2)
    {
        PortWeaponPreview = None;
        return;
    }
    Inventory = UTInventoryManager(CurrentPawn.InvManager);
    Selected = PortSelectedWeapon(CurrentPawn);
    Target = PortWeaponInSlot(CurrentPawn, Slot);
    if (Inventory == None || Target == None || Target.bDeleteMe)
    {
        PortWeaponPreview = None;
        return;
    }
    if ((Request >> 8) == 1)
    {
        if (Target == Selected)
            PortWeaponPreview = None;
        else
            PortWeaponPreview = Target;
        return;
    }
    PortWeaponPreview = None;
    if (Target != Selected)
        Inventory.SetCurrentWeapon(Target);
}

/** Deployment, gunner Jump and the console's look steering for the touch stick. */
function UpdatePortVehicle(Pawn CurrentPawn)
{
    local UTWeaponPawn Seat;
    local UTVehicle V;
    local UTVehicle_Deployable Deployable;
    local rotator VehicleRot, SteerRot;
    local vector AngVel;
    local float Delta, Steer;
    local int Flags;
    if (PortInputMode == 1 && CurrentPawn != None && CurrentPawn.Health > 0)
    {
        Seat = UTWeaponPawn(CurrentPawn);
        if (Seat != None)
            V = Seat.MyVehicle;
        else
            V = UTVehicle(CurrentPawn);
    }
    Deployable = UTVehicle_Deployable(V);
    // UTVehicle_Deployable.DoJump toggles; UTWeaponPawn.DoJump forwards only with bAcceptTurretJump.
    if (Deployable != None && (Seat == None || V.bAcceptTurretJump))
        Flags = Flags | 1;
    if (Deployable != None && Deployable.DeployActivated())
        Flags = Flags | 2;
    if (Seat != None && V != None && V.bAcceptTurretJump)
        Flags = Flags | 4;
    if (Seat == None && V != None && V.bLookSteerOnSimpleControls && PlayerController(V.Controller) != None
        && (Deployable == None || !Deployable.DeployActivated()))
    {
        // The original console branch of UTVehicle.SetInputs, with the view
        // yaw its vehicle simulation copies into DriverViewYaw.
        VehicleRot.Yaw = V.Rotation.Yaw;
        SteerRot.Yaw = V.Controller.Rotation.Yaw;
        Delta = FindDeltaAngle(GetHeadingAngle(vector(SteerRot)), GetHeadingAngle(vector(VehicleRot)));
        if (Delta > V.LookSteerDeadZone)
            Steer = FMin((Delta - V.LookSteerDeadZone) * V.LookSteerSensitivity, 1.0);
        else if (Delta < -V.LookSteerDeadZone)
            Steer = FMax((Delta + V.LookSteerDeadZone) * V.LookSteerSensitivity, -1.0);
        if (V.Mesh != None && V.Mesh.BodyInstance != None)
        {
            AngVel = V.Mesh.BodyInstance.GetUnrealWorldAngularVelocity();
            Steer = FClamp(Steer + AngVel.Z * V.LookSteerDamping, -1.0, 1.0);
        }
        PortSteer = Steer;
        PortSteerReverse = V.DeflectionReverseThresh;
        PortSteerScale = V.ConsoleSteerScale;
        Flags = Flags | 8;
        if (V.bStickDeflectionThrottle)
            Flags = Flags | 16;
        if (V.ForwardVel < 0.0)
            Flags = Flags | 32;
    }
    // The gyroscope's sniper mode: a zoomed scope or a steered Redeemer missile.
    // 16|32 without 8: the host and the touch layer read those two only together
    // with look steering (8), so older layers ignore the combination.
    if ((Flags & 8) == 0 && PortInputMode == 1 && PortScoped(CurrentPawn))
        Flags = Flags | 48;
    PortVehicleFlags = Flags;
}

/** Looking through a scope (UTWeapon zoom) or steering a Redeemer missile. */
static function bool PortScoped(Pawn P)
{
    local UTWeapon W;
    if (P == None || P.Health <= 0)
        return false;
    if (UTRemoteRedeemer(P) != None)
        return true;
    W = UTWeapon(P.Weapon);
    return W != None && W.GetZoomedState() != ZST_NotZoomed;
}

function HandlePortScrollRequest()
{
    local UIObject Target;
    local int Steps, Index;
    local UTGameInteraction Interaction;
    local UTUIOptionList Options;
    local UIList List;
    local UTSimpleList Simple;
    local UIScreenObject Ancestor;
    Target = PortScrollTarget;
    Steps = PortScrollSteps;
    if (Steps != 64 && Steps != 65)
        Steps = Clamp(Steps, -8, 8);
    PortScrollSteps = 0;
    if (Target == None)
    {
        // The host retired the gesture: a tap that did not move may press its row.
        if (PortHovering)
            EndPortDrag(PortDragTarget);
        PortHovering = false;
        PortDragTarget = None;
    }
    if (Target == None || (Steps == 0 && !PortHovering) || PortInputMode != 2
        || UIController == None || UIController.SceneClient == None)
        return;
    Interaction = UTGameInteraction(UIController);
    if (Interaction == None || !Interaction.ShouldProcessUIInput()
        || Target.GetScene() != UIController.SceneClient.GetActiveScene()
        || !Target.IsVisible() || !Target.IsEnabled(0))
        return;
    Ancestor = Target;
    for (Index = 0; Ancestor != None && Index < 64; ++Index)
    {
        if (!Ancestor.IsVisible())
            return;
        Ancestor = Ancestor.GetParent();
    }
    if (Ancestor != None)
        return;
    Options = UTUIOptionList(Target);
    List = UIList(Target);
    Simple = UTSimpleList(Target);
    if (Steps == 64)
    {
        PortHovering = true;
        PortHoverX = -1;
        PortHoverY = -1;
        BeginPortDrag(Target, Options, List, Simple);
    }
    if (PortHovering)
        HoverPortRow(Options, List, Simple);
    if (Steps == 0 || Steps == 64)
        return;
    if (Steps == 65)
    {
        if (Options != None)
            TapPortOption(Options);
        return;
    }
    // Rows from the host scroll the view; the selection stays where it is.
    if (Options != None && Options.GeneratedObjects.Length > 0)
        ScrollPortOptions(Options, PortOptionTop(Options) + Steps);
    else if (List != None)
        List.SetTopIndex(List.TopIndex + Steps, true);
    else if (Simple != None && Simple.List.Length > 0)
        Simple.SelectItem(Clamp(Simple.Selection + Steps, 0, Simple.List.Length - 1));
}

/** The first row an option list shows: the original RepositionOptions keeps
 *  CurrentIndex in the middle, clamped to the ends (UT3.exe 0x698240). */
static function int PortOptionTop(UTUIOptionList Options)
{
    local int Count, Most;
    Count = Options.GeneratedObjects.Length;
    Most = Options.MaxVisibleItems;
    if (Most <= 0 || Count <= Most)
        return 0;
    return Clamp(Options.CurrentIndex - Most / 2, 0, Count - Most);
}

/** Distance between two visible option rows in the list's own pixels. */
static function float PortOptionPitch(UTUIOptionList Options)
{
    local int Index;
    local float Pitch;
    for (Index = 1; Index < Options.GeneratedObjects.Length; ++Index)
    {
        if (Options.GeneratedObjects[Index].OptionObj == None || Options.GeneratedObjects[Index - 1].OptionObj == None
            || !Options.GeneratedObjects[Index].OptionObj.IsVisible() || !Options.GeneratedObjects[Index - 1].OptionObj.IsVisible())
            continue;
        Pitch = Options.GeneratedObjects[Index].OptionY - Options.GeneratedObjects[Index - 1].OptionY;
        if (Pitch > 1)
            return Pitch;
    }
    return Options.GeneratedObjects.Length > 0 ? FMax(Options.GeneratedObjects[0].OptionHeight, 16) : 32.0;
}

/**
 * Shows the rows from Top on without moving the focus: the original list
 * derives its view from CurrentIndex alone, so only that index moves (the
 * native SetSelectedOptionIndex, which also animates the view), and the lit
 * label and the selection bar stay with the focused option (KeepPortFocusLit).
 */
function ScrollPortOptions(UTUIOptionList Options, int Top)
{
    local int Count, Most, Anchor;
    Count = Options.GeneratedObjects.Length;
    Most = Options.MaxVisibleItems;
    if (Most <= 0 || Count <= Most)
        return;
    Anchor = Clamp(Top, 0, Count - Most) + Most / 2;
    if (Anchor == Options.CurrentIndex)
        return;
    Options.SetSelectedOptionIndex(Anchor);
    PortScrolledOptions = Options;
    KeepPortFocusLit(Options);
}

/**
 * The native SetSelectedOptionIndex lights the label of the view's row
 * (SetEnabled, vtable 0x124) and the selection bar follows that row. While the
 * view is away from the focused option, its label stays lit and the bar is
 * hidden; both return when the focused row is the view's row again.
 */
function KeepPortFocusLit(UTUIOptionList Options)
{
    local int Index, Focused;
    local UIObject Control;
    local bool bLit;
    Control = Options.GetFocusedControl(false, 0);
    Focused = Control != None ? Options.GetObjectInfoIndexFromObject(Control) : INDEX_NONE;
    if (Focused == INDEX_NONE)
        Focused = Options.CurrentIndex;
    for (Index = 0; Index < Options.GeneratedObjects.Length; ++Index)
    {
        bLit = Index == Focused;
        if (Options.GeneratedObjects[Index].LabelObj != None && Options.GeneratedObjects[Index].LabelObj.IsEnabled(0) != bLit)
            Options.GeneratedObjects[Index].LabelObj.SetEnabled(bLit, 0);
    }
    if (Options.BGPrefabInstance != None && Options.BGPrefabInstance.IsVisible() != (Focused == Options.CurrentIndex))
        Options.BGPrefabInstance.SetVisibility(Focused == Options.CurrentIndex);
    if (Focused == Options.CurrentIndex)
        PortScrolledOptions = None;
}

/** A finger comes down on a list: what a drag starts from, and what a tap would press. */
function BeginPortDrag(UIObject Target, UTUIOptionList Options, UIList List, UTSimpleList Simple)
{
    PortDragTarget = Target;
    PortDragMoved = false;
    PortDragRow = -1;
    PortDragY = PortCursor(Target).Y;
    if (Options != None)
    {
        PortDragTop = PortOptionTop(Options);
        PortDragIndex = Options.CurrentIndex;
        PortDragRow = PortOptionRow(Options);
    }
    else if (List != None)
    {
        PortDragTop = List.TopIndex;
        PortDragIndex = List.Index;
        PortDragRow = List.CalculateIndexFromCursorLocation(true);
    }
    else if (Simple != None)
        PortDragIndex = Simple.Selection;
}

/**
 * The host retired a list gesture. A tap that did not move, on the row that
 * was already selected, presses it as Enter or a double click does; the first
 * tap only selects. A quick second tap is the original's own double click.
 */
function EndPortDrag(UIObject Target)
{
    local UIList List;
    local WorldInfo CurrentWorld;
    local float Now;
    List = UIList(Target);
    if (List == None || PortDragMoved || PortDragRow < 0)
        return;
    CurrentWorld = GetCurrentWorldInfo();
    Now = CurrentWorld != None ? CurrentWorld.RealTimeSeconds : 0.0;
    if (Now >= PortLastTapTime && Now - PortLastTapTime < 0.6)
    {
        PortLastTapTime = Now;
        return;
    }
    PortLastTapTime = Now;
    if (PortDragRow != PortDragIndex || List.Index != PortDragIndex || List.bSingleClickSubmission
        || UIController == None || UIController.SceneClient == None
        || List.GetScene() != UIController.SceneClient.GetActiveScene() || !List.IsVisible() || !List.IsEnabled(0))
        return;
    List.PlayUISound(List.SubmitDataSuccessCue, 0);
    List.OnSubmitSelection(List, 0);
    List.ActivateEventByClass(0, class'UIEvent_SubmitListData', List);
}

/** The cursor in a widget's own pixels, measured like the original lists do. */
static function vector PortCursor(UIScreenObject Widget)
{
    local int X, Y;
    local vector2D Pixel;
    local vector Position;
    class'UIRoot'.static.GetCursorPosition(X, Y);
    Pixel.X = X;
    Pixel.Y = Y;
    Position = Widget.PixelToCanvas(Pixel);
    Position.X -= Widget.GetPosition(UIFACE_Left, EVALPOS_PixelViewport);
    Position.Y -= Widget.GetPosition(UIFACE_Top, EVALPOS_PixelViewport);
    return Position;
}

/**
 * The option row under the cursor, by the original drag test of
 * UTUIOptionList.CheckAndUpdateDragging; -2 above the list, -3 below it,
 * -1 between rows.
 */
static function int PortOptionRow(UTUIOptionList Options)
{
    local vector Cursor;
    local int Index;
    Cursor = PortCursor(Options);
    if (Cursor.Y < 0)
        return -2;
    if (Cursor.Y > Options.GetBounds(UIORIENT_Vertical, EVALPOS_PixelViewport))
        return -3;
    for (Index = 0; Index < Options.GeneratedObjects.Length; ++Index)
    {
        if (Cursor.Y >= Options.GeneratedObjects[Index].OptionY
            && Cursor.Y <= Options.GeneratedObjects[Index].OptionY + Options.GeneratedObjects[Index].OptionHeight)
            return Index;
    }
    return -1;
}

/**
 * A finger dragged over a list scrolls it the way a phone's lists move: the
 * rows follow the finger (dragging up shows the rows below) and the selection
 * and focus stay where they were. A short jitter is still a tap. Menu lists
 * (UTSimpleList) keep their original pointing: the row under the finger is
 * highlighted, as a mouse over them does.
 */
function HoverPortRow(UTUIOptionList Options, UIList List, UTSimpleList Simple)
{
    local vector Cursor;
    local WorldInfo CurrentWorld;
    local int X, Y, Rows, Most;
    local float Pitch, Moved;
    local bool bMoved;
    class'UIRoot'.static.GetCursorPosition(X, Y);
    bMoved = X != PortHoverX || Y != PortHoverY;
    PortHoverX = X;
    PortHoverY = Y;
    if (Options != None && Options.GeneratedObjects.Length > 0)
    {
        Pitch = PortOptionPitch(Options);
        Moved = PortDragY - PortCursor(Options).Y;
        if (!PortDragMoved && Abs(Moved) < FMax(8, Pitch * 0.35))
            return;
        PortDragMoved = true;
        ScrollPortOptions(Options, PortDragTop + Round(Moved / Pitch));
    }
    else if (List != None && List.GetItemCount() > 0)
    {
        Pitch = FMax(List.GetRowHeight(), 8);
        Moved = PortDragY - PortCursor(List).Y;
        if (!PortDragMoved && Abs(Moved) < FMax(8, Pitch * 0.35))
            return;
        PortDragMoved = true;
        Most = Max(0, List.GetItemCount() - List.GetMaxNumVisibleRows());
        Rows = Clamp(PortDragTop + Round(Moved / Pitch), 0, Most);
        if (Rows != List.TopIndex)
            List.SetTopIndex(Rows, true);
    }
    else if (Simple != None && Simple.List.Length > 0)
    {
        Cursor = PortCursor(Simple);
        // Held beyond a menu list's edge, the selection steps at most every 0.12 s.
        CurrentWorld = GetCurrentWorldInfo();
        if (Cursor.Y < 0 || Cursor.Y > Simple.GetBounds(UIORIENT_Vertical, EVALPOS_PixelViewport))
        {
            if (CurrentWorld == None || (CurrentWorld.RealTimeSeconds - PortEdgeTime < 0.12
                && CurrentWorld.RealTimeSeconds >= PortEdgeTime))
                return;
            PortEdgeTime = CurrentWorld.RealTimeSeconds;
            if (Cursor.Y < 0)
                Simple.SelectItem(Max(Simple.Selection - 1, 0));
            else
                Simple.SelectItem(Min(Simple.Selection + 1, Simple.List.Length - 1));
        }
        else if (bMoved && Simple.MouseInBounds())
            Simple.SelectUnderCursor();
    }
}

/**
 * A tap on an option row changes it: a checkbox toggles, an option button
 * takes its next value and wraps to the first, like its own right arrow, and
 * a combo box selects its next list item the same way. Other rows, such as
 * sliders, are only selected.
 */
function TapPortOption(UTUIOptionList Options)
{
    local int Index;
    local UIObject Option;
    local UICheckbox Check;
    local UTUIOptionButton Button;
    local UIComboBox Combo;
    local int Item;
    Index = PortOptionRow(Options);
    if (Index < 0)
        return;
    Option = Options.GeneratedObjects[Index].OptionObj;
    if (Option == None || !Option.IsEnabled(0))
        return;
    if (Index != Options.CurrentIndex)
        Options.SelectItem(Index, 0);
    Check = UICheckbox(Option);
    Button = UTUIOptionButton(Option);
    Combo = UIComboBox(Option);
    if (Check != None)
        Check.SetValue(!Check.IsChecked(), 0);
    else if (Combo != None)
    {
        // Through its own hidden list: SelectedItemChanged updates the text and notifies.
        if (Combo.ComboList != None && Combo.ComboList.IsHidden() && Combo.ComboList.GetItemCount() > 1)
        {
            Item = Combo.ComboList.Index + 1;
            if (Item >= Combo.ComboList.GetItemCount())
                Item = 0;
            Combo.ComboList.SetIndex(Item);
        }
    }
    else if (Button != None)
    {
        if (Button.HasNextValue())
            Button.OnMoveSelectionRight(0);
        else if (Button.GetCurrentIndex() != 0)
        {
            Button.SetCurrentIndex(0);
            Button.PlayUISound(Button.IncrementCue, 0);
        }
    }
}

/** The tab control of the active scene: the front end's panels or the in-game menu. */
function UITabControl PortActiveTabs()
{
    local UIScene Scene;
    if (UIController == None || UIController.SceneClient == None)
        return None;
    Scene = UIController.SceneClient.GetActiveScene();
    if (UTUIFrontEnd(Scene) != None)
        return UTUIFrontEnd(Scene).TabControl;
    if (UTUIScene_MidGameMenu(Scene) != None)
        return UTUIScene_MidGameMenu(Scene).TabControl;
    return None;
}

/**
 * The tab a press beside the original tab row belongs to: the row is a thin
 * line of captions, so it reaches its own height (at least 4.5% of the screen)
 * above and below, and every gap goes to the nearest button. -1 elsewhere,
 * and on a button itself, which keeps its original click.
 */
function int PortNearTab(UITabControl Tabs)
{
    local int Index, Count, X, Y, Best;
    local UITabPage Page;
    local UITabButton Button;
    local float Left, Top, Right, Bottom, RowTop, RowBottom, RowLeft, RowRight, Distance, BestDistance, Reach;
    local vector2D Size;
    if (Tabs == None || !Tabs.IsVisible())
        return -1;
    class'UIRoot'.static.GetCursorPosition(X, Y);
    Count = Tabs.GetPageCount();
    RowTop = 1000000;
    RowBottom = -1000000;
    RowLeft = 1000000;
    RowRight = -1000000;
    Best = -1;
    BestDistance = 1000000;
    for (Index = 0; Index < Count; ++Index)
    {
        Page = Tabs.GetPageAtIndex(Index);
        Button = Page != None ? Page.GetTabButton() : None;
        if (Button == None || !Button.IsVisible() || !Button.IsEnabled(0))
            continue;
        Left = Button.GetPosition(UIFACE_Left, EVALPOS_PixelViewport);
        Top = Button.GetPosition(UIFACE_Top, EVALPOS_PixelViewport);
        Right = Button.GetPosition(UIFACE_Right, EVALPOS_PixelViewport);
        Bottom = Button.GetPosition(UIFACE_Bottom, EVALPOS_PixelViewport);
        if (X >= Left && X <= Right && Y >= Top && Y <= Bottom)
            return -1;
        RowTop = FMin(RowTop, Top);
        RowBottom = FMax(RowBottom, Bottom);
        RowLeft = FMin(RowLeft, Left);
        RowRight = FMax(RowRight, Right);
        Distance = X < Left ? Left - X : (X > Right ? X - Right : 0.0);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Index;
        }
    }
    GetViewportSize(Size);
    Reach = FMax(RowBottom - RowTop, Size.Y * 0.045);
    if (Best < 0 || RowBottom <= RowTop || Y < RowTop - Reach || Y > RowBottom + Reach
        || X < RowLeft - Reach || X > RowRight + Reach)
        return -1;
    return Best;
}

/** HandleInputKey, before the UI: a press beside the tab row switches the nearest tab. */
function bool PortTabKey(int ControllerId, name Key, EInputEvent EventType, float AmountDepressed, optional bool bGamepad)
{
    local UITabControl Tabs;
    local UITabPage Page;
    local UITabButton Button;
    local int Index;
    if (Key != 'LeftMouseButton' || PortInputMode != 2)
        return false;
    Tabs = PortActiveTabs();
    if (EventType == IE_Released)
    {
        // The UI never saw this press, so it gets no release either.
        Index = PortTabPress;
        PortTabPress = -1;
        if (Index < 0 || Tabs == None || Index >= Tabs.GetPageCount() || PortNearTab(Tabs) != Index)
            return Index >= 0;
        Page = Tabs.GetPageAtIndex(Index);
        Button = Page != None ? Page.GetTabButton() : None;
        if (Page != None && Page != Tabs.ActivePage && Tabs.ActivatePage(Page, 0, true) && Button != None)
            Button.PlayUISound(Button.ClickedCue, 0);
        return true;
    }
    if (EventType != IE_Pressed && EventType != IE_DoubleClick)
        return false;
    PortTabPress = PortNearTab(Tabs);
    return PortTabPress >= 0;
}


function bool ActivateRequestedPortPage(UTUIFrontEnd_SettingsPanels Panels)
{
    local UTGameInteraction Interaction;
    if (!PortMenuPending || !PortPageReady || Panels != PortPanels || PortPage == None || Panels.TabControl == None
        || Panels.OnNotifyOptionChanged == None || Panels.OnMarkProfileDirty == None)
        return false;
    if (UIController == None || UIController.SceneClient == None
        || UIController.SceneClient.GetActiveScene() != Panels)
        return false;
    Interaction = UTGameInteraction(UIController);
    if (Interaction != None && !Interaction.ShouldProcessUIInput())
        return false;
    if (Panels.TabControl.ActivePage != PortPage && !Panels.TabControl.ActivatePage(PortPage, 0, true))
        return false;
    PortMenuPending = false;
    PortRequestedPanelPath = "";
    return true;
}

function HandlePortMenuRequest(float DeltaTime)
{
    local int Index;
    local UTUIFrontEnd_SettingsPanels Panels;
    local UTUIFrontEnd_Settings Settings;
    local UTUIScene SceneOwner;
    local UIScene OpenedScene;
    local UTUIScene_MidGameMenu MidGameMenu;
    local UTUITabPage_MidGameSettings MidGameSettings;
    if (PortMenuRequest != 0)
    {
        PortMenuRequest = 0;
        PortMenuPending = true;
        PortRequestedPanelPath = "";
        NextMenuCheck = 0;
        NextPanelCheck = 0;
    }
    if (!PortMenuPending || UIController == None || UIController.SceneClient == None)
        return;
    // Gameplay callers open the original pause menu first. Never open unpaused
    // settings solely because a host request arrived during gameplay/loading.
    if (!UIController.SceneClient.IsUIActive(2))
        return;
    for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
    {
        Panels = UTUIFrontEnd_SettingsPanels(UIController.SceneClient.ActiveScenes[Index]);
        if (Panels != None)
        {
            PortRequestedPanelPath = PathName(Panels);
            NextPanelCheck = 0;
            ActivateRequestedPortPage(Panels);
            return;
        }
    }
    // The original parent binds publisher/profile-save delegates when opening
    // its panels. Wait for that activation instead of opening panels directly.
    for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
    {
        Settings = UTUIFrontEnd_Settings(UIController.SceneClient.ActiveScenes[Index]);
        if (Settings != None)
        {
            PortRequestedPanelPath = PathName(Settings);
            return;
        }
    }
    // Back may close a newly opened scene before its page becomes activatable.
    // Respect that cancellation instead of reopening it on the following Tick.
    if (PortRequestedPanelPath != "")
    {
        PortMenuPending = false;
        PortRequestedPanelPath = "";
        return;
    }
    NextMenuCheck -= DeltaTime;
    if (NextMenuCheck > 0)
        return;
    NextMenuCheck = 0.25;
    if (!class'UIInteraction'.static.IsMenuLevel())
    {
        for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
        {
            MidGameMenu = UTUIScene_MidGameMenu(UIController.SceneClient.ActiveScenes[Index]);
            if (MidGameMenu != None)
                break;
        }
        if (MidGameMenu == None || MidGameMenu.TabControl == None)
            return;
        MidGameSettings = UTUITabPage_MidGameSettings(MidGameMenu.FindChild('SettingsTab', true));
        if (MidGameSettings != None)
        {
            // Its original ActivatePage opens AdvancedOptions, switches the
            // frontend skin, and installs the matching AdvancedClosed restore.
            MidGameMenu.TabControl.ActivatePage(MidGameSettings, 0, true);
            // Remember the real original opening now: Back may close it before
            // the next viewport Tick has a chance to observe the parent.
            for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
            {
                OpenedScene = UIController.SceneClient.ActiveScenes[Index];
                if (UTUIFrontEnd_Settings(OpenedScene) != None
                    || UTUIFrontEnd_SettingsPanels(OpenedScene) != None)
                {
                    PortRequestedPanelPath = PathName(OpenedScene);
                    break;
                }
            }
        }
        return;
    }
    SceneOwner = UTUIScene(UIController.SceneClient.GetActiveScene());
    if (SceneOwner == None)
        return;
    OpenedScene = SceneOwner.OpenSceneByName(class'UTUIFrontEnd_MainMenu'.default.SettingsScene);
    if (OpenedScene != None)
        PortRequestedPanelPath = PathName(OpenedScene);
}

function HandlePortStartupLogin(float DeltaTime)
{
    local UIScene ActiveScene;
    local UTUIFrontEnd_LoginScreen Login;
    local UTGameInteraction Interaction;
    local OnlineAccountInterface Account;
    local string LoginShown, LoginChanging;
    if (PortStartupLoginHandled || UIController == None || UIController.SceneClient == None)
        return;
    NextStartupLoginCheck -= DeltaTime;
    if (NextStartupLoginCheck > 0)
        return;
    NextStartupLoginCheck = 0.25;
    Interaction = UTGameInteraction(UIController);
    if (Interaction == None || !Interaction.ShouldProcessUIInput())
        return;
    ActiveScene = UIController.SceneClient.GetActiveScene();
    if (ActiveScene == None || !ActiveScene.IsInitialized())
        return;
    Login = UTUIFrontEnd_LoginScreen(ActiveScene);
    if (Login == None)
    {
        // A completed startup must not automate later, manually opened logins.
        if (UTUIFrontEnd_MainMenu(ActiveScene) != None
            && ActiveScene.GetDataStoreStringValue("<Registry:ShownLoginScreen>", LoginShown)
            && LoginShown == "1")
            PortStartupLoginHandled = true;
        return;
    }
    if (Login.GetPlayerOwner() == None || Login.UserNameEditBox == None
        || Login.PasswordEditBox == None || Login.SavePasswordCheckBox == None
        || Login.AutoLoginCheckBox == None || Login.bLoginOnShow)
        return;
    if (Login.GetDataStoreStringValue("<Registry:bChangingLoginStatus>", LoginChanging)
        && LoginChanging == "1")
        return;
    Account = Login.GetAccountInterface();
    if (Account == None || !Account.IsKeyValid())
        return;
    PortStartupLoginHandled = true;
    // Use the original local-login route, including its ordinary error handling.
    Login.OnPlayOffline();
}

function bool PreparePortStores()
{
    local DataStoreClient Stores;
    Stores = UIController.DataStoreManager;
    if (Stores == None)
        return false;
    if (PortStore == None)
    {
        PortStore = AndroidPortDataStore(Stores.FindDataStore('AndroidPort'));
        if (PortStore == None)
        {
            PortStore = Stores.CreateDataStore(class'AndroidPortDataStore');
            if (PortStore == None || !Stores.RegisterDataStore(PortStore))
                return false;
        }
    }
    PortStore.EnsureSettings();
    if (PortValueStore == None)
    {
        PortValueStore = AndroidPortValueStore(Stores.FindDataStore('AndroidPortValues'));
        if (PortValueStore == None)
        {
            PortValueStore = Stores.CreateDataStore(class'AndroidPortValueStore');
            if (PortValueStore == None || !Stores.RegisterDataStore(PortValueStore))
                return false;
        }
    }
    // StringList's native accessors cannot dispatch scalar script fields.
    PortValueStore.Settings = PortStore.Settings;
    return true;
}

static function string FormatPortCDKey(int Seed)
{
    local array<byte> Data;
    local int Second, Checksum, Index, Remainder, Start, Count, GroupIndex;
    local byte Group[5], Encoded[8];
    local string Key;
    Data.Length = 10;
    Checksum = 0;
    Key = "";
    Data[0] = byte(((Seed >>> 16) & 255) ^ 1);
    Data[1] = byte(Seed >>> 24);
    Data[2] = byte(Seed);
    Data[3] = byte(Seed >>> 8);
    Second = 2 * (Seed ^ 0x31443772);
    Data[4] = byte(Second);
    Data[5] = byte(Second >>> 8);
    Data[6] = byte(Second >>> 16);
    Data[7] = byte(Second >>> 24);
    for (Index = 0; Index < 8; ++Index)
        Checksum = Data[Index] - 1664117991 * Checksum;
    Remainder = 0;
    for (Index = 31; Index >= 0; --Index)
    {
        Remainder = (Remainder << 1) | ((Checksum >>> Index) & 1);
        if (Remainder >= 65521)
            Remainder -= 65521;
    }
    Checksum = 1727 ^ Remainder;
    Data[8] = byte(Checksum);
    Data[9] = byte(Checksum >>> 8);
    for (Start = 0; Start < Data.Length; Start += 5)
    {
        for (GroupIndex = 0; GroupIndex < 5; ++GroupIndex)
            Group[GroupIndex] = Data[Start + GroupIndex];
        for (Count = 0; Count < 8; ++Count)
        {
            Encoded[Count] = byte(Group[0] & 31);
            for (GroupIndex = 0; GroupIndex < 4; ++GroupIndex)
                Group[GroupIndex] = byte((Group[GroupIndex] >> 5) | (Group[GroupIndex + 1] << 3));
            Group[4] = Group[4] >> 5;
        }
        for (Count = 0; Count < 8; ++Count)
            Key $= Mid("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", Encoded[Count], 1);
    }
    return Left(Key, 4) $ "-" $ Mid(Key, 4, 4) $ "-" $ Mid(Key, 8, 4) $ "-" $ Mid(Key, 12, 4);
}

static function string GeneratePortCDKey()
{
    return FormatPortCDKey(Rand(65536) | (Rand(65536) << 16));
}

function bool AcceptStartupCDKey(UTUIFrontEnd_CDKey Scene)
{
    local int Index;
    local string Key;
    if (PortStartupCDKeyHandled || Scene == None || Scene.AccountInterface == None)
        return false;
    for (Index = 0; Index < 4; Index++)
        if (Scene.CDKeyEditBox[Index] == None)
            return false;
    Key = GeneratePortCDKey();
    Scene.CDKeyEditBox[0].SetValue(Left(Key, 4), 0, true);
    Scene.CDKeyEditBox[1].SetValue(Mid(Key, 5, 4), 0, true);
    Scene.CDKeyEditBox[2].SetValue(Mid(Key, 10, 4), 0, true);
    Scene.CDKeyEditBox[3].SetValue(Mid(Key, 15, 4), 0, true);
    PortStartupCDKeyHandled = true;
    Scene.OnAccept();
    return true;
}

event GameSessionEnded()
{
    if (PortMenuScale != None)
        PortMenuScale.Reset();
    PortPanels = None;
    PortPage = None;
    PortPageReady = false;
    PortMenuPending = false;
    PortMenuRequest = 0;
    PortRequestedPanelPath = "";
    PortScrollSteps = 0;
    PortScrollTarget = None;
    PortHovering = false;
    PortDragTarget = None;
    PortScrolledOptions = None;
    PortTabPress = -1;
    PortStartupCDKeyHandled = false;
    NextPanelCheck = 0;
    Super.GameSessionEnded();
}

/**
 * A read-only edit box keeps the end of its text in view. The page's combos
 * took their texts before the page had a size, which left nothing in view;
 * setting each text again once the page is placed shows it from the start.
 */
function RefreshPortCombos()
{
    local int Index;
    local UIComboBox Combo;
    local string Value;
    for (Index = 0; Index < PortPage.OptionList.GeneratedObjects.Length; ++Index)
    {
        Combo = UIComboBox(PortPage.OptionList.GeneratedObjects[Index].OptionObj);
        if (Combo == None || Combo.ComboEditbox == None)
            continue;
        Value = Combo.ComboEditbox.GetValue();
        Combo.ComboEditbox.SetValue("", , true);
        Combo.ComboEditbox.SetValue(Value, , true);
    }
}

/**
 * A finger is the pointer on Android. The original UI tracks the pointer and
 * its clicks only while it shows a mouse cursor (bDisplayCursor and a cursor
 * without an image stop both), so the cursor in use is drawn transparent.
 */
function HidePortCursor()
{
    local UITexture Cursor;
    if (UIController == None || UIController.SceneClient == None)
        return;
    Cursor = UIController.SceneClient.CurrentMouseCursor;
    if (Cursor == None || (Cursor == PortHiddenCursor && UIController.SceneClient.ActiveSkin == PortCursorSkin))
        return;
    if (PortCursorStyle == None)
    {
        PortCursorStyle = new(Self) class'UIStyle_Image';
        PortCursorStyle.StyleColor = MakeLinearColor(1.0, 1.0, 1.0, 0.0);
        PortCursorStyle.bEnabled = true;
    }
    Cursor.SetImageStyle(PortCursorStyle);
    PortHiddenCursor = Cursor;
    PortCursorSkin = UIController.SceneClient.ActiveSkin;
}

event Tick(float DeltaTime)
{
    local int Index;
    local UTUIFrontEnd_SettingsPanels Panels;
    local UIScene TemplateScene;
    local UTUITabPage_Options TemplatePage;
    Super.Tick(DeltaTime);
    if (!PortTabsHooked)
    {
        HandleInputKey = PortTabKey;
        PortTabsHooked = true;
    }
    HandlePortStartupLogin(DeltaTime);
    UpdatePortInputContext();
    HidePortCursor();
    HandlePortScrollRequest();
    if (PortScrolledOptions != None)
    {
        if (UIController == None || UIController.SceneClient == None || !PortScrolledOptions.IsVisible()
            || PortScrolledOptions.GetScene() != UIController.SceneClient.GetActiveScene())
            PortScrolledOptions = None;
        else
            KeepPortFocusLit(PortScrolledOptions);
    }
    HandlePortMenuRequest(DeltaTime);
    NextPanelCheck -= DeltaTime;
    if (NextPanelCheck > 0 || UIController == None || UIController.SceneClient == None)
        return;
    NextPanelCheck = 0.25;
    if (!PreparePortStores())
        return;
    for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
        if (AcceptStartupCDKey(UTUIFrontEnd_CDKey(UIController.SceneClient.ActiveScenes[Index])))
            break;
    if (PortMenuScale == None)
        PortMenuScale = new(Self) class'AndroidPortMenuScale';
    if (UIController.SceneClient.IsUIActive(2))
        PortMenuScale.Apply(UIController.SceneClient.ActiveScenes, PortStore.Settings.uiScale);
    else
        PortMenuScale.Prune(UIController.SceneClient.ActiveScenes);
    for (Index = UIController.SceneClient.ActiveScenes.Length - 1; Index >= 0; --Index)
    {
        Panels = UTUIFrontEnd_SettingsPanels(UIController.SceneClient.ActiveScenes[Index]);
        if (Panels == None || Panels.TabControl == None || Panels.HUDTab == None)
            continue;
        if (Panels != PortPanels || PortPage == None)
        {
            PortStore.PrepareSelections();
            // Live option lists contain expanded prefab instances with runtime outers.
            TemplateScene = UIScene(Panels.ObjectArchetype);
            if (TemplateScene == None)
                return;
            TemplatePage = UTUITabPage_Options(TemplateScene.FindChild('pnlHUDSettings', true));
            if (TemplatePage == None || TemplatePage == Panels.HUDTab || TemplatePage.IsInitialized())
                return;
            PortPage = UTUITabPage_Options(Panels.TabControl.CreateTabPage(class'UTUITabPage_HUDSettings', TemplatePage));
            if (PortPage == None)
                return;
            PortPageReady = false;
            // Runtime widgets initialize only after insertion; PostInitialize sets OptionList.
            if (!Panels.TabControl.InsertPage(PortPage, 0, INDEX_NONE, false))
            {
                PortPage = None;
                return;
            }
            PortPanels = Panels;
        }
        if (!PortPageReady)
        {
            if (PortPage.OptionList == None)
                return;
            PortPage.bAllowResetToDefaults = false;
            PortPage.OnOptionChanged = OnPortOptionChanged;
            PortPage.OptionList.SetDataStoreBinding("<UTOptions:AndroidPort>");
            PortPage.SetDataStoreBinding("ANDROID");
            PortPageReady = true;
            PortComboWait = 0;
            LogInternal("AndroidPort settings page ready: " $ Panels.TabControl.GetPageCount()
                $ " pages, " $ PortPage.OptionList.GeneratedObjects.Length $ " options");
        }
        // The page's first check after it is shown and placed (its combos were filled before it had a size).
        if (PortComboWait < 2 && Panels.TabControl.ActivePage == PortPage && ++PortComboWait == 2)
            RefreshPortCombos();
        ActivateRequestedPortPage(Panels);
        return;
    }
    PortPanels = None;
    PortPage = None;
    PortPageReady = false;
}

defaultproperties
{
    PortInputEpoch=1
    PortTabPress=-1
}

function OnPortOptionChanged(UIScreenObject InObject, name OptionName, int PlayerIndex)
{
    local UIDataStorePublisher Publisher;
    local array<UIDataStore> BoundStores;
    Publisher = UIDataStorePublisher(InObject);
    if (Publisher == None || PortStore == None)
        return;
    if (Publisher.SaveSubscriberValue(BoundStores))
        PortStore.CommitCollection(name(Mid(string(OptionName), 12)));
}
