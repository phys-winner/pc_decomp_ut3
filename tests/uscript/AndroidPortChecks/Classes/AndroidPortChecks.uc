class AndroidPortChecks extends Commandlet
    dependson(UTUIFrontEnd_SettingsVideoAdvanced);

function bool Check(bool Condition, string Label)
{
    if (!Condition)
        LogInternal("ANDROID_PORT_CHECK_FAILED " $ Label);
    return Condition;
}

event int Main(string Params)
{
    local AndroidPortDataStore Store;
    local AndroidPortValueStore ValueStore;
    local AndroidPortMenuScale MenuScale;
    local UTSimpleList ScaleList;
    local UIComp_DrawString ScaleText;
    local UILabel ScaleLabel;
    local UIComp_DrawString ScaleLabelText;
    local UIStyleResolver ScaleResolver;
    local UITabControl ScaleTabs;
    local array<UIScene> ScaleScenes;
    local LinearColor ErrorColor;
    local UTUIDataProvider_StringArray CellProvider;
    local UIList CellList;
    local UIListString CellText;
    local int CellIndex;
    local AndroidPortCellFixture CellFixture;
    local int CellNodeCount;
    local string CellOriginalText;
    local array<string> OriginalProviderValues;
    local UIRoot.UIProviderScriptFieldValue Value;
    local array<UIDataProvider.UIDataProviderField> Fields;
    local GameEngine TestEngine;
    local AndroidPortViewportClient Viewport;
    local UIScene FirstScene, SecondScene;
    local UTUIFrontEnd_SettingsPanels TemplatePanels;
    local UITabControl TemplateTabs;
    local UTUITabPage_Options TemplatePage, ClonedPage;
    local UIInteraction TestInteraction;
    local GameUISceneClient TestSceneClient;
    local UIScene RuntimeScene;
    local UTUIFrontEnd_SettingsPanels ActivePanels;
    local UTUIFrontEnd_Settings OriginalSettings;
    local delegate<UTUIFrontEnd_SettingsPanels.OnMarkProfileDirty> ExpectedDirty;
    local delegate<UTUIFrontEnd_SettingsPanels.OnNotifyOptionChanged> ExpectedPublisher;
    local int Epoch;
    local string LanguageLabel, DisplayValue;
    local AndroidPortOptionsFocusFixture FocusFixture;
    local UTUIOptionList FocusOptions;
    local UICheckbox FocusCheck;
    local UIState_Focused FocusState;
    local UISlider TestSlider;
    local AndroidPortCommandFixture CommandPanel;
    local string LongText, FieldText;
    local int TextIndex;
    local UTUIFrontEnd_SettingsVideoAdvanced AdvancedVideo;
    Store = new(None) class'AndroidPortDataStore';
    if (!Check(Len(Store.LayoutCaption) == 19 && Asc(Left(Store.LayoutCaption, 1)) == 1056
        && Asc(Right(Store.LayoutCaption, 1)) == 1071, "localized layout caption preserves Unicode")) return 1;
    Store.EnsureSettings();
    TestEngine = new(None) class'GameEngine';
    Viewport = new(TestEngine) class'AndroidPortViewportClient';
    Viewport.PortStore = Store;
    AdvancedVideo = new(None) class'UTUIFrontEnd_SettingsVideoAdvanced';
    AdvancedVideo.OptionsPage = new(AdvancedVideo) class'UTUITabPage_Options';
    AdvancedVideo.OptionsPage.OptionList = new(AdvancedVideo.OptionsPage) class'UTUIOptionList';
    Viewport.PortAdvancedScene = AdvancedVideo;
    AdvancedVideo.SetVideoSettingValue(PVS_TextureDetail, 1);
    AdvancedVideo.SetVideoSettingValue(PVS_StaticDecals, 0);
    AdvancedVideo.SetVideoSettingValue(PVS_CompositeDynamicLights, 0);
    AdvancedVideo.SetVideoSettingValue(PVS_DropParticleDistortion, 0);
    AdvancedVideo.SetVideoSettingValue(PVS_SkeletalMeshLODBias, -2);
    AdvancedVideo.SetVideoSettingValue(PVS_ParticleLODBias, -2);
    AdvancedVideo.SetVideoSettingValue(PVS_ShadowFilterQualityBias, -1);
    if (!Check(Viewport.OnPortMaximumGraphics(None, 0), "maximum graphics action")) return 1;
    LogInternal("ANDROID_PORT_MAX_VALUES " $ AdvancedVideo.GetVideoSettingValue(PVS_TextureDetail)
        $ " " $ AdvancedVideo.GetVideoSettingValue(PVS_ScreenPercentage)
        $ " " $ AdvancedVideo.GetVideoSettingValue(PVS_EnableMotionBlur)
        $ " " $ AdvancedVideo.GetVideoSettingValue(PVS_DepthOfField)
        $ " " $ AdvancedVideo.GetVideoSettingValue(PVS_Bloom)
        $ " " $ AdvancedVideo.GetVideoSettingValue(PVS_QualityBloom));
    if (!Check(AdvancedVideo.GetVideoSettingValue(PVS_TextureDetail) == 5
        && AdvancedVideo.GetVideoSettingValue(PVS_ScreenPercentage) == 100
        && AdvancedVideo.GetVideoSettingValue(PVS_EnableMotionBlur) == 0
        && AdvancedVideo.GetVideoSettingValue(PVS_DepthOfField) == 0
        && AdvancedVideo.GetVideoSettingValue(PVS_Bloom) == 0
        && AdvancedVideo.GetVideoSettingValue(PVS_QualityBloom) == 0,
        "original engine applies maximum graphics without blur/bloom")) return 1;
    // These checkboxes disable quality-reducing approximations; their checked value is correct.
    if (!Check(AdvancedVideo.GetVideoSettingValue(PVS_CompositeDynamicLights) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_DropParticleDistortion) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_OneFrameThreadLag) == 1,
        "maximum disables light compositing and particle distortion dropping")) return 1;
    if (!Check(AdvancedVideo.GetVideoSettingValue(PVS_StaticDecals) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_DynamicDecals) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_DynamicLights) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_DynamicShadows) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_LightEnvironmentShadows) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_DirectionalLightmaps) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_Distortion) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_SpeedTreeLeaves) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_SpeedTreeFronds) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_LensFlares) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_FogVolumes) == 1
        && AdvancedVideo.GetVideoSettingValue(PVS_HighPolyChars) == 1,
        "maximum enables lighting, shadows, geometry and visual effects")) return 1;
    if (!Check(AdvancedVideo.GetVideoSettingValue(PVS_DetailMode) == 2
        && AdvancedVideo.GetVideoSettingValue(PVS_SkeletalMeshLODBias) == 0
        && AdvancedVideo.GetVideoSettingValue(PVS_ParticleLODBias) == 0
        && AdvancedVideo.GetVideoSettingValue(PVS_ShadowFilterQualityBias) == 1,
        "maximum restores highest original detail and LOD values")) return 1;
    ValueStore = new(None) class'AndroidPortValueStore';
    ValueStore.Settings = Store.Settings;
    if (!Check(ValueStore.Settings == Store.Settings, "scalar and collection stores share settings")) return 1;
    Store.Settings.touchSensitivity = 0.22;
    if (!Check(ValueStore.GetFieldValue("touchSensitivity", Value)
        && Value.PropertyType == DATATYPE_RangeProperty
        && Value.RangeValue.bIntRange && Abs(Value.RangeValue.CurrentValue - 22.0) < 0.0001
        && Abs(Value.RangeValue.MinValue - 4.0) < 0.0001
        && Abs(Value.RangeValue.MaxValue - 300.0) < 0.0001
        && Abs(Value.RangeValue.NudgeValue - 2.0) < 0.0001, "integer percent range read")) return 1;
    Value.RangeValue.CurrentValue = 22.0;
    if (!Check(ValueStore.SetFieldValue("touchSensitivity", Value)
        && Abs(Store.Settings.touchSensitivity - 0.22) < 0.0001, "percent UI preserves canonical sensitivity")) return 1;
    Value.RangeValue.CurrentValue = 65.0;
    if (!Check(ValueStore.SetFieldValue("touchOpacity", Value)
        && Abs(Store.Settings.touchOpacity - 0.65) < 0.0001, "percent UI preserves canonical opacity")) return 1;
    Value.RangeValue.CurrentValue = 999.0;
    if (!Check(ValueStore.SetFieldValue("touchSensitivity", Value)
        && Store.Settings.touchSensitivity == 3.0, "upper clamp")) return 1;
    Value.RangeValue.CurrentValue = -999.0;
    if (!Check(ValueStore.SetFieldValue("touchOpacity", Value)
        && Abs(Store.Settings.touchOpacity - 0.2) < 0.0001, "lower clamp")) return 1;
    Value.StringValue = "1";
    if (!Check(ValueStore.SetFieldValue("touchLeftHanded", Value)
        && Store.Settings.touchLeftHanded, "boolean write")) return 1;
    if (!Check(ValueStore.SetFieldValue("touchWasd", Value) && Store.Settings.touchWasd
        && ValueStore.GetFieldValue("touchWasd", Value) && Value.StringValue == "1", "W/A/S/D cross switch")) return 1;
    if (!Check(!ValueStore.GetFieldValue("editLayout", Value), "no layout editor field")) return 1;
    if (!Check(!ValueStore.SetFieldValue("UnknownField", Value)
        && !ValueStore.GetFieldValue("UnknownField", Value), "unknown fields rejected")) return 1;

    Store.Settings.renderer = "dxvk-turnip";
    if (!Check(Store.GetFieldIndex('renderer') == INDEX_NONE, "renderer belongs to launcher only")) return 1;
    Value.RangeValue.CurrentValue = 22.0;
    if (!Check(ValueStore.SetFieldValue("touchSensitivity", Value)
        && Store.Settings.renderer == "dxvk-turnip", "scalar save retains current collection value")) return 1;
    if (!Check(Store.Settings.renderer == "dxvk-turnip", "Turnip choice")) return 1;
    LanguageLabel = Chr(1056) $ Chr(1091) $ Chr(1089) $ Chr(1089) $ Chr(1082) $ Chr(1080) $ Chr(1081);
    Store.RemoveStrByIndex('language', 1, 1, true);
    Store.InsertStr('language', LanguageLabel, 1, true);
    Store.Settings.language = "rUs";
    Store.PrepareSelections();
    if (!Check(Store.GetCurrentValueIndex('language') == 1
        && Store.GetCurrentValue('language', DisplayValue) && DisplayValue == LanguageLabel,
        "localized label selection uses engine code")) return 1;
    Store.CommitCollection('language');
    if (!Check(Store.Settings.language == "RUS", "localized language choice saves engine code")) return 1;
    Store.Choices.Length = 1;
    Store.Choices[0].Tag = 'touchVehicleProfile';
    Store.Choices[0].Labels.AddItem("P0");
    Store.Choices[0].Labels.AddItem("P1");
    Store.Choices[0].Labels.AddItem("P2");
    Store.Choices[0].Labels.AddItem("P3");
    Store.Choices[0].Labels.AddItem("P4");
    Store.Settings.touchVehicleProfile = 3;
    Store.PrepareSelections();
    if (!Check(Store.StringData[Store.GetFieldIndex('touchVehicleProfile')].Strings.Length == 5
        && Store.StringData[Store.GetFieldIndex('touchVehicleProfile')].Strings[0] == "P0"
        && Store.GetCurrentValueIndex('touchVehicleProfile') == 3
        && Store.GetCurrentValue('touchVehicleProfile', DisplayValue) && DisplayValue == "P3",
        "configured choice labels fill the list")) return 1;
    Store.Choices.Length = 0;
    Store.DisplayFps.Length = 0;
    Store.DisplayFps.AddItem(60);
    Store.DisplayFps.AddItem(90);
    Store.DisplayFps.AddItem(120);
    Store.Settings.fpsLimit = 40;
    Store.PrepareSelections();
    if (!Check(Store.Settings.fpsLimit == 60 && Store.GetCurrentValueIndex('fpsLimit') == 0,
        "unsupported legacy FPS is saved as a display mode")) return 1;
    Store.Settings.fpsLimit = 0;
    Store.PrepareSelections();
    if (!Check(Store.Settings.fpsLimit == 120 && Store.GetCurrentValueIndex('fpsLimit') == 2,
        "legacy unlimited FPS becomes the highest display mode")) return 1;
    Store.Settings.fpsLimit = 105;
    Store.PrepareSelections();
    if (!Check(Store.Settings.fpsLimit == 120 && Store.GetCurrentValueIndex('fpsLimit') == 2,
        "display FPS migration agrees with Android nearest rate")) return 1;
    Store.SetCurrentValueIndex('fpsLimit', 1);
    Store.CommitCollection('fpsLimit');
    if (!Check(Store.Settings.fpsLimit == 90, "display FPS choice")) return 1;
    if (!Check(ValueStore.GetFieldValue("weaponBarScale", Value)
        && Value.RangeValue.MinValue == 50 && Value.RangeValue.MaxValue == 100, "weapon bar range")) return 1;
    Value.RangeValue.CurrentValue = 5;
    if (!Check(ValueStore.SetFieldValue("weaponBarScale", Value) && Store.Settings.weaponBarScale == 0.5, "bar lower clamp")) return 1;
    Value.RangeValue.CurrentValue = 150;
    if (!Check(ValueStore.SetFieldValue("weaponWheelScale", Value) && Store.Settings.weaponWheelScale == 1.5, "wheel scale")) return 1;
    if (!Check(Store.Settings.touchWeaponSwipe && Store.Settings.touchWeaponButtonSwipe
        && !Store.Settings.touchWeaponTap && Store.Settings.touchVisible, "touch defaults")) return 1;
    Value.StringValue = "0";
    if (!Check(ValueStore.SetFieldValue("touchWeaponSwipe", Value) && !Store.Settings.touchWeaponSwipe
        && Store.Settings.touchWeaponButtonSwipe, "bar swipe does not disable button swipe")) return 1;
    Value.StringValue = "1";
    if (!Check(ValueStore.SetFieldValue("touchWeaponSwipe", Value), "restore bar swipe")) return 1;
    Value.StringValue = "0";
    if (!Check(ValueStore.SetFieldValue("touchWeaponButtonSwipe", Value) && Store.Settings.touchWeaponSwipe
        && !Store.Settings.touchWeaponButtonSwipe, "button swipe does not disable bar swipe")) return 1;
    if (!Check(!ValueStore.GetFieldValue("renderScale", Value), "duplicate render scale removed")) return 1;
    Value.StringValue = "1";
    if (!Check(ValueStore.SetFieldValue("fsr1", Value) && ValueStore.SetFieldValue("antialias", Value)
        && Store.Settings.fsr1 && Store.Settings.antialias, "post-process options reach canonical settings")) return 1;
    if (!Check(ValueStore.SetFieldValue("frameGeneration", Value) && Store.Settings.frameGeneration,
        "frame generation reaches canonical settings")) return 1;
    Store.Settings.touchGyroMode = -1;
    Store.Settings.touchGyro = 2;
    if (!Check(Store.Settings.GyroMode() == 3, "an older build's always-on gyroscope stays always on")) return 1;
    Store.Settings.touchGyro = 1;
    if (!Check(Store.Settings.GyroMode() == 1, "an older build's secondary-fire gyroscope becomes sniper mode")) return 1;
    Store.PrepareSelections();
    if (!Check(Store.GetCurrentValueIndex('touchGyro') == 1, "gyroscope selection from the older setting")) return 1;
    Store.SetCurrentValueIndex('touchGyro', 2);
    Store.CommitCollection('touchGyro');
    if (!Check(Store.Settings.touchGyroMode == 2 && Store.Settings.GyroMode() == 2, "gyroscope while firing")) return 1;
    Store.SetCurrentValueIndex('touchGyro', 4);
    Store.CommitCollection('touchGyro');
    if (!Check(Store.Settings.touchGyroMode == 4 && Store.Settings.GyroMode() == 4, "gyroscope firing or aiming without a held activation key")) return 1;
    Store.SetCurrentValueIndex('touchVehicleProfile', 4);
    Store.CommitCollection('touchVehicleProfile');
    if (!Check(Store.Settings.touchVehicleProfile == 4, "turret profile")) return 1;
    ValueStore.GetSupportedScriptFields(Fields);
    if (!Check(Fields.Length == 19, "scalar fields discoverable")) return 1;
    if (!Check(ValueStore.GetFieldValue("uiScale", Value)
        && Value.RangeValue.CurrentValue == 100 && Value.RangeValue.MinValue == 100
        && Value.RangeValue.MaxValue == 150 && Value.RangeValue.NudgeValue == 5,
        "menu scale percent range")) return 1;
    Value.RangeValue.CurrentValue = 125;
    if (!Check(ValueStore.SetFieldValue("uiScale", Value)
        && Store.Settings.uiScale == 1.25, "menu scale shares canonical settings")) return 1;

    MenuScale = new(None) class'AndroidPortMenuScale';
    FirstScene = new(None) class'UIScene';
    TestEngine = new(None) class'GameEngine';
    FocusFixture = new(TestEngine) class'AndroidPortOptionsFocusFixture';
    FocusOptions = new(FirstScene) class'UTUIOptionList';
    FocusCheck = new(FirstScene) class'UICheckbox';
    FocusOptions.GeneratedObjects.Length = 2;
    FocusOptions.GeneratedObjects[1].OptionObj = FocusCheck;
    FocusState = new(FocusCheck) class'UIState_Focused';
    FocusFixture.PortScrollTarget = FocusOptions;
    FocusFixture.PortOptionState(FocusCheck, 0, FocusState);
    if (!Check(FocusFixture.ObservedOptions == FocusOptions && FocusFixture.PortOptionFocused == 1
        && FocusOptions.CurrentIndex == 0, "touch focus finds generated sibling options without scrolling")) return 1;
    TestSlider = new(FirstScene) class'UISlider';
    TestSlider.SliderValue.MinValue = 100;
    TestSlider.SliderValue.MaxValue = 150;
    TestSlider.SliderValue.CurrentValue = 100;
    TestSlider.OnValueChanged = FocusFixture.SliderChanged;
    TestSlider.SetValue(0.5, true);
    if (!Check(TestSlider.GetValue() == 125 && FocusFixture.SliderNotifications == 0,
        "native SetValue updates slider without notifying its publisher")) return 1;
    TestSlider.NotifyValueChanged(0);
    if (!Check(FocusFixture.SliderNotifications == 1, "touch slider explicitly notifies its publisher")) return 1;
    FocusFixture.PortCommandColumns.Length = 1;
    FocusFixture.PortCommandColumns[0].Left = 100;
    FocusFixture.PortCommandColumns[0].Top = 200;
    FocusFixture.PortCommandColumns[0].Width = 300;
    FocusFixture.PortCommandColumns[0].RowHeight = 40;
    FocusFixture.PortCommandColumns[0].Column = 1;
    FocusFixture.PortCommandColumns[0].Count = 3;
    if (!Check(FocusFixture.PortCommandHit(100, 200) == 17 && FocusFixture.PortCommandHit(399, 279) == 18
        && FocusFixture.PortCommandHit(400, 210) == 0 && FocusFixture.PortCommandHit(120, 320) == 0
        && FocusFixture.PortCommandHit(99, 200) == 0, "command touch rows use the drawn columns and exclusive bounds")) return 1;
    CommandPanel = new(FirstScene) class'AndroidPortCommandFixture';
    CommandPanel.MyLists[0].Values.Length = 2;
    CommandPanel.MyLists[0].Values[1].EmoteTag = 'Taunt';
    CommandPanel.MyLists[1].Values.Length = 3;
    CommandPanel.MyLists[2].Values.Length = 1;
    CommandPanel.NeedsPlayer = true;
    if (!Check(FocusFixture.PortCommandChoose(CommandPanel, 0, 1) && CommandPanel.FilledCategory == 'Taunt'
        && CommandPanel.MyLists[0].Selection == 1 && CommandPanel.FocusedList == 1,
        "touch category reuses the original command chain")) return 1;
    if (!Check(FocusFixture.PortCommandChoose(CommandPanel, 1, 2) && CommandPanel.FocusedList == 2
        && CommandPanel.MyLists[1].Selection == 2, "touch command advances to its recipient list")) return 1;
    CommandPanel.LastItem = true;
    if (!Check(FocusFixture.PortCommandChoose(CommandPanel, 2, 0) && CommandPanel.Commands == 1,
        "touch recipient calls the original command implementation")) return 1;
    CommandPanel.MyLists[1].Values[0].bDisabled = true;
    if (!Check(!FocusFixture.PortCommandChoose(CommandPanel, 1, 0) && !FocusFixture.PortCommandChoose(CommandPanel, 3, 0)
        && !FocusFixture.PortCommandChoose(CommandPanel, 1, 7) && CommandPanel.Commands == 1,
        "disabled and invalid command rows cannot execute")) return 1;
    ScaleList = new(FirstScene) class'UTSimpleList';
    ScaleList.DefaultCellHeight = 40;
    ScaleList.ResScaling.Y = 2;
    ScaleText = new(ScaleList) class'UIComp_DrawString';
    ScaleResolver = UIStyleResolver(ScaleText);
    ScaleList.AddStyleSubscriber(ScaleResolver);
    MenuScale.Texts.Length = 1;
    MenuScale.Texts[0].Component = ScaleText;
    MenuScale.Texts[0].Scene = FirstScene;
    MenuScale.Texts[0].Scale.X = 0.75;
    MenuScale.Texts[0].Scale.Y = 1;
    MenuScale.Texts[0].Customization = ScaleText.TextStyleCustomization;
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.25);
    if (!Check(ScaleList.DefaultCellHeight == 50 && ScaleList.GetDefaultCellHeight() == 100
        && ScaleText.TextStyleCustomization.DrawScale[0] == 0.9375
        && ScaleText.TextStyleCustomization.DrawScale[1] == 1.25,
        "native list row and text scale 125 percent")) return 1;
    ErrorColor.R = 1;
    ErrorColor.G = 0.2;
    ErrorColor.B = 0.3;
    ErrorColor.A = 0.9;
    ScaleText.SetColor(ErrorColor);
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.5);
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.5);
    if (!Check(ScaleList.DefaultCellHeight == 60
        && ScaleText.TextStyleCustomization.DrawScale[0] == 1.125
        && ScaleText.TextStyleCustomization.bOverrideDrawColor
        && ScaleText.TextStyleCustomization.DrawColor.G == 0.2,
        "menu scale does not accumulate")) return 1;
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.0);
    if (!Check(ScaleList.DefaultCellHeight == 40
        && !ScaleText.TextStyleCustomization.bOverrideScale
        && ScaleText.TextStyleCustomization.bOverrideDrawColor
        && ScaleText.TextStyleCustomization.DrawColor.G == 0.2
        && ScaleList.Position.Value[0] == 0 && ScaleList.Position.Value[1] == 0
        && ScaleList.Position.Value[2] == 1 && ScaleList.Position.Value[3] == 1,
        "menu scale restores native font policy and anchored bounds")) return 1;
    ScaleTabs = new(FirstScene) class'UITabControl';
    ScaleTabs.TabButtonSize.Value = 40;
    MenuScale.ScaleWidget(ScaleTabs, FirstScene, 1.25);
    MenuScale.ScaleWidget(ScaleTabs, FirstScene, 1.5);
    MenuScale.ScaleWidget(ScaleTabs, FirstScene, 1.0);
    if (!Check(ScaleTabs.TabButtonSize.Value == 40 && ScaleTabs.bUpdateLayout,
        "tab geometry restores through native layout update")) return 1;
    ScaleScenes.AddItem(FirstScene);
    MenuScale.Prune(ScaleScenes);
    if (!Check(MenuScale.Widgets.Length == 2 && MenuScale.Texts.Length == 1,
        "live menu scale caches retained")) return 1;
    ScaleScenes.Length = 0;
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.25);
    MenuScale.Prune(ScaleScenes);
    if (!Check(MenuScale.Widgets.Length == 0 && MenuScale.Texts.Length == 0
        && ScaleList.DefaultCellHeight == 40,
        "closed scenes restore and release scale caches")) return 1;
    ScaleList.RemoveStyleSubscriber(ScaleResolver);
    MenuScale.ScaleWidget(ScaleList, FirstScene, 1.25);
    if (!Check(ScaleList.DefaultCellHeight == 50, "reopened scene starts from original scale")) return 1;
    MenuScale.Reset();
    if (!Check(ScaleList.DefaultCellHeight == 40 && MenuScale.Widgets.Length == 0,
        "session cleanup restores original menu geometry")) return 1;
    ScaleLabel = new(FirstScene) class'UILabel';
    ScaleLabelText = new(ScaleLabel) class'UIComp_DrawString';
    ScaleLabelText.SetAutoScaling(UIAUTOSCALE_Justified, 0.6);
    ScaleLabelText.DisableCustomAutoScaling();
    MenuScale.Texts.Length = 1;
    MenuScale.Texts[0].Component = ScaleLabelText;
    MenuScale.Texts[0].Scene = FirstScene;
    MenuScale.Texts[0].Scale.X = 1;
    MenuScale.Texts[0].Scale.Y = 1;
    MenuScale.Texts[0].Customization = ScaleLabelText.TextStyleCustomization;
    MenuScale.ScaleText(ScaleLabelText, FirstScene, 1.5);
    if (!Check(ScaleLabelText.TextStyleCustomization.bOverrideAutoScale
        && ScaleLabelText.TextStyleCustomization.AutoScaling.AutoScaleMode == UIAUTOSCALE_None
        && ScaleLabelText.TextStyleCustomization.DrawScale[0] == 1.5,
        "native label explicit scale overrides inherited auto fit")) return 1;
    ScaleLabelText.SetColor(ErrorColor);
    MenuScale.ScaleText(ScaleLabelText, FirstScene, 1.0);
    if (!Check(!ScaleLabelText.TextStyleCustomization.bOverrideAutoScale
        && !ScaleLabelText.TextStyleCustomization.bOverrideScale
        && ScaleLabelText.TextStyleCustomization.bOverrideDrawColor
        && ScaleLabelText.TextStyleCustomization.DrawColor.G == 0.2,
        "native label restores inherited auto fit and preserves dynamic color")) return 1;
    ScaleLabelText.SetAutoScaling(UIAUTOSCALE_Justified, 0.75);
    MenuScale.Texts[0].Customization = ScaleLabelText.TextStyleCustomization;
    MenuScale.ScaleText(ScaleLabelText, FirstScene, 1.25);
    MenuScale.ScaleText(ScaleLabelText, FirstScene, 1.0);
    if (!Check(ScaleLabelText.TextStyleCustomization.bOverrideAutoScale
        && ScaleLabelText.TextStyleCustomization.AutoScaling.AutoScaleMode == UIAUTOSCALE_Justified
        && ScaleLabelText.TextStyleCustomization.AutoScaling.MinScale == 0.75,
        "native label restores explicit original auto fit policy")) return 1;
    MenuScale.Reset();
    CellFixture = new(None) class'AndroidPortCellFixture';
    CellList = new(FirstScene) class'UIList';
    if (CellList.CellDataComponent == None)
        CellList.CellDataComponent = new(CellList) class'UIComp_ListPresenter';
    CellFixture.Provider = new(CellFixture) class'UTUIDataProvider_StringArray';
    CellProvider = CellFixture.Provider;
    CellProvider.Strings.AddItem("OpenGL ES");
    CellProvider.Strings.AddItem("Vulkan (DXVK)");
    CellProvider.Strings.AddItem("Vulkan (Turnip)");
    OriginalProviderValues = CellProvider.Strings;
    if (!Check(CellProvider != None, "native Strings provider bound to cell fixture")) return 1;
    CellList.RowHeight.Value = 16;
    CellList.RowAutoSizeMode = CELLAUTOSIZE_Constrain;
    CellList.CellLinkType = LINKED_Columns;
    CellList.CellPadding.Value = 2;
    CellList.CellSpacing.Value = 1;
    CellList.CellDataComponent.ListItems.Length = 3;
    for (CellIndex = 0; CellIndex < 3; ++CellIndex)
    {
        CellFixture.Bindings[CellIndex].DataSourceProvider = UIListElementCellProvider(CellProvider);
        CellFixture.Bindings[CellIndex].DataSourceTag = 'Strings';
        CellFixture.Bindings[CellIndex].DataSourceIndex = CellIndex;
        CellText = new(CellList) class'UIListString';
        CellText.StringExtent.Y = 54;
        CellList.CellDataComponent.ListItems[CellIndex].Cells.Length = 1;
        CellList.CellDataComponent.ListItems[CellIndex].Cells[0].ValueString = CellText;
        // Full bound-node formatting is covered by the original PE oracle.
        CellOriginalText = CellText.GetValue(true);
        CellNodeCount = CellText.Nodes.Length;
        MenuScale.ScaleWidget(CellList, FirstScene, 1.25);
        if (!Check(CellText.GetValue(true) == CellOriginalText
            && CellText.Nodes.Length == CellNodeCount
            && CellText.StringStyleData.TextAutoScaling.AutoScaleMode == UIAUTOSCALE_None
            && !CellText.bAdjustStringScale
            && CellProvider.Strings[CellIndex] == OriginalProviderValues[CellIndex]
            && CellFixture.Bindings[CellIndex].DataSourceTag == 'Strings'
            && CellFixture.Bindings[CellIndex].DataSourceIndex == CellIndex
            && CellFixture.Bindings[CellIndex].DataSourceProvider == UIListElementCellProvider(CellProvider)
            && CellList.RowHeight.Value == 56.5 && CellList.RowAutoSizeMode == CELLAUTOSIZE_None
            && CellList.CellPadding.Value == 2.5
            && CellList.CellSpacing.Value == 1.25 && CellList.CellDataComponent.bReapplyFormatting,
            "native list geometry preserves original cell policy and provider binding")) return 1;
    }
    CellList.RowAutoSizeMode = CELLAUTOSIZE_Constrain;
    CellList.RowHeight.Value = 0;
    MenuScale.ScaleWidget(CellList, FirstScene, 1.25);
    if (!Check(CellList.RowHeight.Value == 56.5 && CellList.RowAutoSizeMode == CELLAUTOSIZE_None,
        "regenerated list rows recover their scaled font pitch without changing bindings")) return 1;
    MenuScale.ScaleWidget(CellList, FirstScene, 1.5);
    MenuScale.ScaleWidget(CellList, FirstScene, 1.0);
    if (!Check(CellList.RowHeight.Value == 16 && CellList.RowAutoSizeMode == CELLAUTOSIZE_Constrain
        && CellList.CellPadding.Value == 2
        && CellList.CellSpacing.Value == 1, "native list geometry round trip")) return 1;
    if (!Check(CellProvider.GetElementCount() == 3, "native Strings provider contents preserved")) return 1;
    MenuScale.Reset();

    if (!Check(class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTPawn') == 0
        && class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTVehicle_Hoverboard') == 3
        && class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTAirVehicle') == 2
        && class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTWeaponPawn') == 4
        && class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTVehicle_TrackTurretBase') == 4
        && class'AndroidPortViewportClient'.static.ClassifyPortVehicle(class'UTVehicle_Manta') == 1,
        "vehicle hierarchy classification")) return 1;
    TestEngine = new(None) class'GameEngine';
    Viewport = new(TestEngine) class'AndroidPortViewportClient';
    if (!Check(class'AndroidPortViewportClient'.static.FormatPortCDKey(0x5e7a4250) == "5VZA-FBTJ-LH96-PHJQ",
        "CD key matches original decoder and checksum domain")) return 1;
    Viewport.HandlePortStartupLogin(0);
    if (!Check(!Viewport.PortStartupLoginHandled, "startup login waits without an initialized UI")) return 1;
    Viewport.PortStartupLoginHandled = true;
    Viewport.NextStartupLoginCheck = 0.5;
    Viewport.HandlePortStartupLogin(1.0);
    if (!Check(Viewport.NextStartupLoginCheck == 0.5, "completed startup login does not repeat")) return 1;
    Viewport.PortStartupLoginHandled = false;
    Viewport.PortScrollTarget = ScaleList;
    Viewport.PortScrollSteps = 5;
    Viewport.HandlePortScrollRequest();
    if (!Check(Viewport.PortScrollTarget == ScaleList && Viewport.PortScrollSteps == 0,
        "scroll acknowledgement retains the gesture target without dispatch in an invalid context")) return 1;
    Viewport.GameSessionEnded();
    if (!Check(Viewport.PortScrollTarget == None && Viewport.PortScrollSteps == 0,
        "session cleanup releases captured scroll target")) return 1;
    Viewport.PortMenuRequest = 7;
    Viewport.HandlePortMenuRequest(0);
    if (!Check(Viewport.PortMenuRequest == 0 && Viewport.PortMenuPending,
        "menu mailbox acknowledges and waits for original UI")) return 1;
    Viewport.HandlePortMenuRequest(1);
    if (!Check(Viewport.PortMenuPending && !Viewport.ActivateRequestedPortPage(None),
        "menu waits safely without an initialized scene")) return 1;
    Viewport.PortMenuPending = false;
    ActivePanels = new(None) class'UTUIFrontEnd_SettingsPanels';
    ActivePanels.TabControl = new(ActivePanels) class'UTUITabControl';
    Viewport.PortPanels = ActivePanels;
    Viewport.PortPage = new(ActivePanels.TabControl) class'UTUITabPage_Options';
    Viewport.PortPageReady = true;
    ActivePanels.TabControl.ActivePage = Viewport.PortPage;
    Viewport.PortMenuPending = true;
    if (!Check(!Viewport.ActivateRequestedPortPage(ActivePanels) && Viewport.PortMenuPending,
        "port activation waits for original publisher and profile callbacks")) return 1;
    OriginalSettings = new(None) class'UTUIFrontEnd_Settings';
    OriginalSettings.OnPanelsSceneOpened(ActivePanels, true);
    ExpectedDirty = OriginalSettings.MarkDirty;
    ExpectedPublisher = OriginalSettings.OnSettingValueChanged;
    if (!Check(ActivePanels.OnMarkProfileDirty == ExpectedDirty
        && ActivePanels.OnNotifyOptionChanged == ExpectedPublisher,
        "original settings parent installs original dirty and publisher-save callbacks")) return 1;
    ActivePanels.OnMarkProfileDirty(true);
    if (!Check(!Viewport.ActivateRequestedPortPage(ActivePanels) && Viewport.PortMenuPending
        && ActivePanels.TabControl.ActivePage == Viewport.PortPage,
        "even a ready page waits without an initialized top scene and does not change focus")) return 1;
    Viewport.PortPanels = None;
    Viewport.PortPage = None;
    Viewport.PortPageReady = false;
    FirstScene = new(None) class'UIScene';
    SecondScene = new(None) class'UIScene';
    if (!Check(Viewport.PortInputEpoch == 1, "input epoch starts at one")) return 1;
    Viewport.PublishPortInputContext(None, None, None, FirstScene, true);
    Epoch = Viewport.PortInputEpoch;
    if (!Check(Viewport.PortInputMode == 2 && Epoch == 2, "capturing UI publishes mode")) return 1;
    Viewport.PublishPortInputContext(None, None, None, FirstScene, true);
    if (!Check(Viewport.PortInputEpoch == Epoch, "unchanged input context is stable")) return 1;
    Viewport.PublishPortInputContext(None, None, None, SecondScene, true);
    if (!Check(Viewport.PortInputEpoch == Epoch + 1, "menu identity invalidates holds")) return 1;
    Viewport.PublishPortInputContext(None, None, None, None, false);
    if (!Check(Viewport.PortInputMode == 0 && Viewport.PortInputEpoch == Epoch + 2,
        "missing controller publishes loading")) return 1;
    Viewport.PortInputEpoch = 2147483647;
    Viewport.PublishPortInputContext(None, None, None, FirstScene, true);
    if (!Check(Viewport.PortInputEpoch == 1, "input epoch wrap stays positive")) return 1;
    Viewport.PortScrollTarget = ScaleList;
    Viewport.PortScrollSteps = 3;
    Viewport.UpdatePortInputContext();
    if (!Check(Viewport.PortInputMode == 0 && Viewport.PortScrollTarget == None
        && Viewport.PortScrollSteps == 0, "input context change retires pending scroll")) return 1;
    Viewport.ViewportConsole = new(Viewport) class'Console';
    Viewport.ViewportConsole.bEnableUI = false;
    Viewport.UpdatePortInputContext();
    if (!Check(Viewport.PortInputMode == 0, "closed original console does not capture")) return 1;
    Viewport.ViewportConsole.GotoState('Open');
    Viewport.UpdatePortInputContext();
    Epoch = Viewport.PortInputEpoch;
    if (!Check(Viewport.PortInputMode == 2, "open original console captures without UIScene")) return 1;
    Viewport.UpdatePortInputContext();
    if (!Check(Viewport.PortInputEpoch == Epoch, "unchanged console context is stable")) return 1;
    if (!Check(Viewport.ViewportConsole.InputChar(0, "z")
        && Viewport.ViewportConsole.TypedStr == "", "original opening key guard captures text until keydown")) return 1;
    Viewport.ViewportConsole.InputKey(0, 'A', IE_Pressed);
    if (!Check(Viewport.ViewportConsole.InputChar(0, "qa_")
        && Viewport.ViewportConsole.TypedStr == "qa_", "original console consumes InputChar after key guard")) return 1;
    Viewport.ViewportConsole.GotoState('Typing');
    Viewport.UpdatePortInputContext();
    if (!Check(Viewport.PortInputMode == 2, "original typing console captures without UIScene")) return 1;
    // Native text dialog requests: the console's line, field limits, passwords, longer texts.
    if (!Check(class'AndroidPortViewportClient'.static.PortTextRequest(8, 0, false, true) == (4 | (256 << 8))
        && class'AndroidPortViewportClient'.static.PortTextRequest(5, 12, true, false) == (1 | (12 << 8))
        && class'AndroidPortViewportClient'.static.PortTextRequest(12, 12, false, false) == (12 << 8)
        && class'AndroidPortViewportClient'.static.PortTextRequest(0, 300, false, false) == (256 << 8)
        && class'AndroidPortViewportClient'.static.PortTextRequest(13, 12, false, false) == (2 | 8 | (256 << 8))
        && class'AndroidPortViewportClient'.static.PortTextRequest(300, 0, true, false) == (1 | 2 | 8 | (256 << 8))
        && class'AndroidPortViewportClient'.static.PortTextRequest(300, 0, false, true) == (2 | 8 | (256 << 8)),
        "text requests: console line, field limits, password, texts longer than the dialog shows")) return 1;
    // Chat types into the same line: the original StartTyping("Say ") of UTPlayerController.Talk.
    Viewport.ViewportConsole.StartTyping("Say helo");
    Viewport.PortTextCommand = 1;
    Viewport.HandlePortText();
    if (!Check(Viewport.PortTextCommand == -1 && Viewport.PortTextTarget == None
        && Viewport.PortTextFlags == (4 | (256 << 8)) && Viewport.PortTextValue == "Say helo"
        && Len(Viewport.PortTextInput) == 256, "the console's line goes to the native dialog")) return 1;
    Viewport.PortTextInput = "Say hello~ " $ Chr(0x43F) $ Chr(0x440) $ "`!";
    Viewport.PortTextCommand = 2;
    Viewport.HandlePortText();
    if (!Check(Viewport.PortTextCommand == 0 && Viewport.ViewportConsole.TypedStr == "Say hello !"
        && Viewport.ViewportConsole.TypedStrPos == 11 && Viewport.PortTextFlags == 0,
        "a commit replaces the console's line, typed as the original AppendInputText types")) return 1;
    Viewport.PortTextCommand = 1;
    Viewport.HandlePortText();
    Viewport.PortTextInput = "";
    Viewport.PortTextCommand = 2;
    Viewport.HandlePortText();
    if (!Check(Viewport.ViewportConsole.TypedStr == "" && Viewport.ViewportConsole.TypedStrPos == 0,
        "an empty commit clears the console's line")) return 1;
    Viewport.ViewportConsole.SetInputText("say kept");
    Viewport.ViewportConsole.SetCursorPos(8);
    Viewport.PortTextCommand = 1;
    Viewport.HandlePortText();
    Viewport.PortTextEpoch = Viewport.PortInputEpoch + 1;
    Viewport.PortTextInput = "say stale";
    Viewport.PortTextCommand = 2;
    Viewport.HandlePortText();
    if (!Check(Viewport.ViewportConsole.TypedStr == "say kept" && Viewport.PortTextCommand == 0,
        "a commit of an older input context leaves the line")) return 1;
    for (TextIndex = 0; TextIndex < 300; ++TextIndex)
        LongText $= "a";
    Viewport.ViewportConsole.SetInputText(LongText);
    Viewport.PortTextCommand = 1;
    Viewport.HandlePortText();
    if (!Check(Viewport.PortTextFlags == (2 | 8 | (256 << 8)) && Viewport.PortTextValue == ""
        && Viewport.PortTextTarget == None && Viewport.PortTextCommand == -1,
        "a 300-unit line opens the dialog empty, typed into the original InputChar")) return 1;
    Viewport.PortTextInput = "x";
    Viewport.PortTextCommand = 2;
    Viewport.HandlePortText();
    if (!Check(Viewport.ViewportConsole.TypedStr == LongText && Viewport.PortTextCommand == 0,
        "no whole-line commit for a line the dialog does not show")) return 1;
    Viewport.ViewportConsole.SetInputText("");
    // The original UIEditBox.CharacterSet rule (UT3.exe 0x00FE2130), one string through each set.
    FieldText = "Ab9.-+ _!" $ Chr(0xE9) $ Chr(0xD7) $ Chr(0x42F) $ Chr(0x20AC) $ Chr(0x2013);
    if (!Check(class'AndroidPortCharacterSet'.static.Filter(FieldText, 0) == FieldText
        && class'AndroidPortCharacterSet'.static.Filter(FieldText, 1) == ("Ab9 " $ Chr(0xE9) $ Chr(0x42F) $ Chr(0x20AC))
        && class'AndroidPortCharacterSet'.static.Filter(FieldText, 2) == ("Ab" $ Chr(0xE9) $ Chr(0xD7))
        && class'AndroidPortCharacterSet'.static.Filter(FieldText, 3) == "9.-+"
        && class'AndroidPortCharacterSet'.static.Filter(FieldText, 4) == ("Ab9.-+" $ Chr(0xE9) $ Chr(0xD7))
        && class'AndroidPortCharacterSet'.static.Filter(FieldText, 5) == "",
        "original CharacterSet rule: All, NoSpecial, AlphaOnly, NumericOnly, AlphaNumeric, unknown")) return 1;
    if (!Check(class'AndroidPortCharacterSet'.static.Filter(Chr(0x8C) $ Chr(0x152) $ Chr(0x9F) $ Chr(0x178), 2)
        == (Chr(0x8C) $ Chr(0x9F)) && class'AndroidPortCharacterSet'.static.Filter("12:30", 3) == "1230"
        && class'AndroidPortCharacterSet'.static.Filter("a" $ Chr(0x2019) $ Chr(0x2116) $ Chr(0xFF01), 1) == ("a" $ Chr(0x2116)),
        "original Latin-1 letters, numeric fields, punctuation above U+00FF")) return 1;
    Viewport.ViewportConsole.GotoState('');
    Viewport.UpdatePortInputContext();
    if (!Check(Viewport.PortInputMode == 0 && Viewport.PortInputEpoch > Epoch,
        "closing original console retires input context")) return 1;
    TemplatePanels = UTUIFrontEnd_SettingsPanels(DynamicLoadObject(
        class'UTUIFrontEnd_Settings'.default.SettingsPanelsScene, class'UIScene', true));
    if (!Check(TemplatePanels != None, "original settings scene template exists")) return 1;
    TemplateTabs = UITabControl(TemplatePanels.FindChild('pnlTabControl', true));
    TemplatePage = UTUITabPage_Options(TemplatePanels.FindChild('pnlHUDSettings', true));
    if (!Check(TemplateTabs != None && TemplatePage != None, "original tab prefab exists")) return 1;
    TestInteraction = new(Viewport) class'UIInteraction';
    TestSceneClient = new(TestInteraction) class'GameUISceneClient';
    RuntimeScene = TestSceneClient.CreateScene(TemplatePanels.Class,, TemplatePanels);
    if (!Check(RuntimeScene != None && RuntimeScene.ObjectArchetype == TemplatePanels
        && !TemplatePage.IsInitialized(), "runtime scene preserves uninitialized resource archetype")) return 1;
    ClonedPage = UTUITabPage_Options(TemplateTabs.CreateTabPage(class'AndroidPortOptionsPage', TemplatePage));
    if (!Check(ClonedPage != None && !ClonedPage.IsInitialized() && ClonedPage.OptionList == None
        && ClonedPage.FindChild('lstOptions', true) != None,
        "runtime clone needs insertion before PostInitialize sets OptionList")) return 1;
    if (!Check(TemplateTabs.InsertPage(ClonedPage, 0, INDEX_NONE, false),
        "raw resource page inserts without prefab name conflicts")) return 1;
    ClonedPage.Initialize(TemplatePanels, ClonedPage.GetTabButton(TemplateTabs));
    if (!Check(ClonedPage.IsInitialized() && !TemplatePage.IsInitialized(),
        "raw resource clone initializes without mutating its template")) return 1;
    LogInternal("ANDROID_PORT_CHECKS_PASS");
    return 0;
}

defaultproperties
{
    IsClient=false
    IsEditor=true
    IsServer=false
    LogToConsole=false
}
