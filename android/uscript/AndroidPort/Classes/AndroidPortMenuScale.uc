class AndroidPortMenuScale extends Object;

struct PortWidgetScale
{
    var UIObject Widget;
    var UIScene Scene;
    var float Height, Padding, Spacing;
    var float AppliedScale;
    // A simple list grown down: the stored bottom face it is restored to, and
    // the top and row count it was fitted for.
    var bool bGrown;
    var float BottomValue;
    var EPositionEvalType BottomType;
    var float FitTop;
    var int FitCount;
};

struct PortTextScale
{
    var UIComp_DrawString Component;
    var UIScene Scene;
    var Vector2D Scale;
    var UITextStyleOverride Customization;
    var float AppliedScale;
};

var transient array<PortWidgetScale> Widgets;
var transient array<PortTextScale> Texts;

function Reset()
{
    local int Index;
    for (Index = 0; Index < Widgets.Length; ++Index)
        ScaleWidget(Widgets[Index].Widget, Widgets[Index].Scene, 1.0);
    for (Index = 0; Index < Texts.Length; ++Index)
        ScaleText(Texts[Index].Component, Texts[Index].Scene, 1.0);
    Widgets.Length = 0;
    Texts.Length = 0;
}

function Prune(const out array<UIScene> Scenes)
{
    local int Index;
    for (Index = Widgets.Length - 1; Index >= 0; --Index)
        if (Scenes.Find(Widgets[Index].Scene) == INDEX_NONE)
        {
            ScaleWidget(Widgets[Index].Widget, Widgets[Index].Scene, 1.0);
            Widgets.Remove(Index, 1);
        }
    for (Index = Texts.Length - 1; Index >= 0; --Index)
        if (Scenes.Find(Texts[Index].Scene) == INDEX_NONE)
        {
            ScaleText(Texts[Index].Component, Texts[Index].Scene, 1.0);
            Texts.Remove(Index, 1);
        }

}

function ScaleText(UIComp_DrawString Component, UIScene Scene, float Scale)
{
    local int Index;
    local UICombinedStyleData Style;
    if (Component == None)
        return;
    for (Index = 0; Index < Texts.Length; ++Index)
        if (Texts[Index].Component == Component)
            break;
    if (Index == Texts.Length)
    {
        if (Scale == 1.0 || Component.Outer == None
            || Component.Outer.GetCurrentState(0) == None
            || !Component.GetFinalStringStyle(Style))
            return;
        Texts.Length = Index + 1;
        Texts[Index].Component = Component;
        Texts[Index].Scene = Scene;
        Texts[Index].Scale = Style.TextScale;
        Texts[Index].Customization = Component.TextStyleCustomization;
    }
    if (Texts[Index].AppliedScale == Scale
        && (Scale == 1.0 || (Component.TextStyleCustomization.DrawScale[UIORIENT_Horizontal] == Texts[Index].Scale.X * Scale
            && Component.TextStyleCustomization.DrawScale[UIORIENT_Vertical] == Texts[Index].Scale.Y * Scale
            && (UILabel(Component.Outer) == None || (Component.TextStyleCustomization.bOverrideAutoScale
                && Component.TextStyleCustomization.AutoScaling.AutoScaleMode == UIAUTOSCALE_None)))))
        return;
    Component.SetScale(UIORIENT_Horizontal, Texts[Index].Scale.X * Scale);
    Component.SetScale(UIORIENT_Vertical, Texts[Index].Scale.Y * Scale);
    if (UILabel(Component.Outer) != None)
    {
        // Auto-fit replaces explicit label scaling in the original formatter.
        if (Scale > 1.0)
            Component.SetAutoScaling(UIAUTOSCALE_None);
        else if (Texts[Index].Customization.bOverrideAutoScale)
            Component.SetAutoScaling(Texts[Index].Customization.AutoScaling.AutoScaleMode,
                Texts[Index].Customization.AutoScaling.MinScale);
        else
            Component.DisableCustomAutoScaling();
    }
    if (Scale == 1.0 && !Texts[Index].Customization.bOverrideScale)
        Component.DisableCustomScale();
    Texts[Index].AppliedScale = Scale;
}

function ScaleWidget(UIObject Widget, UIScene Scene, float Scale)
{
    local int Index, ComponentIndex;
    local UTSimpleList Simple;
    local UTUIOptionList Options;
    local UIList List;
    local UITabControl Tabs;
    local UTUIButtonBar Bar;
    local Object StyleObject;
    Simple = UTSimpleList(Widget);
    Options = UTUIOptionList(Widget);
    List = UIList(Widget);
    Tabs = UITabControl(Widget);
    Bar = UTUIButtonBar(Widget);
    // The original button bar applies its own text scale while laying out buttons.
    if (UTUIButtonBarButton(Widget) == None)
        for (ComponentIndex = 0; ComponentIndex < Widget.StyleSubscribers.Length; ++ComponentIndex)
        {
            StyleObject = Widget.StyleSubscribers[ComponentIndex];
            ScaleText(UIComp_DrawString(StyleObject), Scene, Scale);
        }
    if (Simple == None && Options == None && List == None && Tabs == None && Bar == None)
        return;
    for (Index = 0; Index < Widgets.Length; ++Index)
        if (Widgets[Index].Widget == Widget)
            break;
    if (Index == Widgets.Length)
    {
        if (Scale == 1.0)
            return;
        Widgets.Length = Index + 1;
        Widgets[Index].Widget = Widget;
        Widgets[Index].Scene = Scene;
        if (Simple != None)
            Widgets[Index].Height = Simple.DefaultCellHeight;
        else if (Options != None)
        {
            Widgets[Index].Height = Options.DefaultOptionHeight;
            Widgets[Index].Padding = Options.DefaultOptionPadding;
        }
        else if (List != None)
        {
            Widgets[Index].Height = List.RowHeight.Value;
            Widgets[Index].Padding = List.CellPadding.Value;
            Widgets[Index].Spacing = List.CellSpacing.Value;
        }
        else if (Tabs != None)
            Widgets[Index].Height = Tabs.TabButtonSize.Value;
        else if (Bar != None)
        {
            Widgets[Index].Height = Bar.ButtonStringScale.X;
            Widgets[Index].Padding = Bar.ButtonStringScale.Y;
        }
    }
    if (Simple != None)
    {
        FitSimpleList(Simple, Scene, Index, Scale);
        return;
    }
    if (Widgets[Index].AppliedScale == Scale && (Bar == None || !Bar.bResetButtonScale))
        return;
    if (Options != None)
    {
        Options.DefaultOptionHeight = Widgets[Index].Height * Scale;
        Options.DefaultOptionPadding = Widgets[Index].Padding * Scale;
        if (Options.BGPrefabInstance != None && Options.GeneratedObjects.Length > 0)
            Options.RepositionOptions();
    }
    else if (List != None)
    {
        List.RowHeight.Value = Widgets[Index].Height * Scale;
        List.CellPadding.Value = Widgets[Index].Padding * Scale;
        List.CellSpacing.Value = Widgets[Index].Spacing * Scale;
        if (List.CellDataComponent != None)
            List.CellDataComponent.bReapplyFormatting = true;
    }
    else if (Tabs != None)
    {
        Tabs.TabButtonSize.Value = Widgets[Index].Height * Scale;
        Tabs.RequestLayoutUpdate();
    }
    else if (Bar != None)
    {
        Bar.ButtonStringScale.X = Widgets[Index].Height * Scale;
        Bar.ButtonStringScale.Y = Widgets[Index].Padding * Scale;
        Bar.bResetButtonScale = true;
    }
    Widget.RequestSceneUpdate(false, true, true);
    Widgets[Index].AppliedScale = Scale;
}

/**
 * A simple list draws its rows from its top but takes taps only inside its
 * bounds, and it scrolls once the rows outgrow them. A list whose rows fit at
 * 1.0 keeps fitting: it grows down into the free space below it, and its rows
 * grow only as far as that room allows.
 */
function FitSimpleList(UTSimpleList Simple, UIScene Scene, int Index, float Scale)
{
    local float Top, Window, Content, Room, Fit;
    local Vector2D Viewport;
    local int Item;
    if (Scale == 1.0)
    {
        RestoreSimpleList(Simple, Index);
        SetSimpleRows(Simple, Widgets[Index].Height);
        Widgets[Index].AppliedScale = 1.0;
        return;
    }
    Top = Simple.GetPosition(UIFACE_Top, EVALPOS_PixelViewport);
    if (Widgets[Index].AppliedScale == Scale && Widgets[Index].FitCount == Simple.List.Length
        && Abs(Top - Widgets[Index].FitTop) < 1)
        return;
    if (Widgets[Index].bGrown)
    {
        // The layout moved (the viewport size, the rows): fit again once the
        // scene has placed the list's own bottom.
        RestoreSimpleList(Simple, Index);
        Widgets[Index].AppliedScale = 0;
        return;
    }
    Window = Simple.GetPosition(UIFACE_Bottom, EVALPOS_PixelViewport) - Top;
    if (Window < 1 || !Simple.GetViewportSize(Viewport) || Viewport.Y < 1)
    {
        // Not placed yet: the rows take the scale, and the next pass fits them.
        SetSimpleRows(Simple, Widgets[Index].Height * Scale);
        Widgets[Index].AppliedScale = Scale;
        Widgets[Index].FitCount = -1;
        return;
    }
    // The rows at 1.0 with the selection in the middle, as the original sizes them (SizeList).
    for (Item = 0; Item < Simple.List.Length; ++Item)
        Content += Simple.GetItemScale(Item, Simple.List.Length / 2);
    Content *= Widgets[Index].Height * Viewport.Y / 768.0;
    Fit = Scale;
    Room = Window;
    if (Content > 0 && Content <= Window + 0.5 && Content * Scale > Window + 0.5)
    {
        if (!IsDockedBelow(Simple, Scene))
            Room = FClamp(RoomBelow(Simple, Scene, Top, Viewport.Y) - Top, Window, Content * Scale);
        Fit = FClamp(Room / Content, 1.0, Scale);
    }
    if (Room > Window + 0.5)
    {
        Widgets[Index].BottomValue = Simple.Position.Value[UIFACE_Bottom];
        Widgets[Index].BottomType = Simple.Position.ScaleType[UIFACE_Bottom];
        Widgets[Index].bGrown = true;
        Simple.SetPosition(Top + Room, UIFACE_Bottom, EVALPOS_PixelViewport);
    }
    SetSimpleRows(Simple, Widgets[Index].Height * Fit);
    Simple.RequestSceneUpdate(false, true, true);
    Widgets[Index].AppliedScale = Scale;
    Widgets[Index].FitCount = Simple.List.Length;
    Widgets[Index].FitTop = Top;
}

static function SetSimpleRows(UTSimpleList Simple, float CellHeight)
{
    Simple.DefaultCellHeight = CellHeight;
    Simple.bInvalidated = true;
    // The selection bar keeps the old row pitch until the selection moves.
    if (Simple.ResScaling.Y > 0)
        Simple.RefreshBarPosition();
}

function RestoreSimpleList(UTSimpleList Simple, int Index)
{
    if (!Widgets[Index].bGrown)
        return;
    Simple.Position.Value[UIFACE_Bottom] = Widgets[Index].BottomValue;
    Simple.Position.ScaleType[UIFACE_Bottom] = Widgets[Index].BottomType;
    Widgets[Index].bGrown = false;
    Simple.RequestSceneUpdate(false, true, true);
}

/** A docked bottom face follows its target, so such a list cannot grow. */
static function bool IsDockedBelow(UTSimpleList Simple, UIScene Scene)
{
    local array<UIObject> Children;
    local int Index;
    if (Simple.IsDockedTo(Scene, UIFACE_Bottom))
        return true;
    Children = Scene.GetChildren(true);
    for (Index = 0; Index < Children.Length; ++Index)
        if (Children[Index] != Simple && Simple.IsDockedTo(Children[Index], UIFACE_Bottom))
            return true;
    return false;
}

/** The row a list may grow down to: the top of the first shown widget below
 *  the list's top that shares its columns, or the bottom of the viewport. */
static function float RoomBelow(UTSimpleList Simple, UIScene Scene, float Top, float Limit)
{
    local array<UIObject> Children;
    local UIScreenObject Ancestor;
    local float Left, Right, WidgetTop;
    local int Index, Depth;
    Left = Simple.GetPosition(UIFACE_Left, EVALPOS_PixelViewport);
    Right = Simple.GetPosition(UIFACE_Right, EVALPOS_PixelViewport);
    Children = Scene.GetChildren(true);
    for (Index = 0; Index < Children.Length; ++Index)
    {
        if (Children[Index] == None || Children[Index] == Simple || Simple.ContainsChild(Children[Index]))
            continue;
        WidgetTop = Children[Index].GetPosition(UIFACE_Top, EVALPOS_PixelViewport);
        if (WidgetTop <= Top + 1 || WidgetTop >= Limit
            || Children[Index].GetPosition(UIFACE_Bottom, EVALPOS_PixelViewport) <= WidgetTop + 1
            || Children[Index].GetPosition(UIFACE_Left, EVALPOS_PixelViewport) >= Right - 1
            || Children[Index].GetPosition(UIFACE_Right, EVALPOS_PixelViewport) <= Left + 1)
            continue;
        Ancestor = Children[Index];
        for (Depth = 0; Ancestor != None && Depth < 64 && Ancestor.IsVisible(); ++Depth)
            Ancestor = Ancestor.GetParent();
        if (Ancestor == None)
            Limit = WidgetTop;
    }
    return Limit;
}

function Apply(const out array<UIScene> Scenes, float Scale)
{
    local int SceneIndex, WidgetIndex;
    local array<UIObject> Children;
    Scale = FClamp(Scale, 1.0, 1.5);
    Prune(Scenes);
    if (Scale == 1.0 && Widgets.Length == 0 && Texts.Length == 0)
        return;
    for (SceneIndex = 0; SceneIndex < Scenes.Length; ++SceneIndex)
    {
        if (Scenes[SceneIndex] == None || !Scenes[SceneIndex].IsInitialized())
            continue;
        Children = Scenes[SceneIndex].GetChildren(true);
        for (WidgetIndex = 0; WidgetIndex < Children.Length; ++WidgetIndex)
            if (Children[WidgetIndex].IsInitialized())
                ScaleWidget(Children[WidgetIndex], Scenes[SceneIndex], Scale);
    }
}
