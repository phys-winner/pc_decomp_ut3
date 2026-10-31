class AndroidPortValueStore extends UIDataStore;

var AndroidPortSettings Settings;

function EnsureSettings()
{
    if (Settings == None)
        Settings = new(None) class'AndroidPortSettings';
}

function AddScriptField(out array<UIDataProviderField> Fields, name FieldName, EUIDataProviderFieldType Type)
{
    local UIDataProviderField Field;
    Field.FieldTag = FieldName;
    Field.FieldType = Type;
    Fields.AddItem(Field);
}

event GetSupportedScriptFields(out array<UIDataProviderField> out_Fields)
{
    AddScriptField(out_Fields, 'touchSensitivity', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'touchAltSensitivity', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'touchScale', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'touchOpacity', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'touchGyroSensitivity', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'uiScale', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'renderScale', DATATYPE_RangeProperty);
    AddScriptField(out_Fields, 'touchLeftHanded', DATATYPE_Property);
    AddScriptField(out_Fields, 'touchInvertY', DATATYPE_Property);
    AddScriptField(out_Fields, 'touchWasd', DATATYPE_Property);
}

function bool ReadRange(name Field, out UIRangeData Range)
{
    Range.bIntRange = true;
    switch (Field)
    {
    case 'touchSensitivity':
        Range.MinValue = 0.04; Range.MaxValue = 3.0; Range.NudgeValue = 0.02;
        Range.CurrentValue = Settings.touchSensitivity; break;
    case 'touchAltSensitivity':
        Range.MinValue = 0.1; Range.MaxValue = 3.0; Range.NudgeValue = 0.05;
        Range.CurrentValue = Settings.touchAltSensitivity; break;
    case 'touchScale':
        Range.MinValue = 0.65; Range.MaxValue = 1.6; Range.NudgeValue = 0.05;
        Range.CurrentValue = Settings.touchScale; break;
    case 'touchOpacity':
        Range.MinValue = 0.2; Range.MaxValue = 1.0; Range.NudgeValue = 0.05;
        Range.CurrentValue = Settings.touchOpacity; break;
    case 'touchGyroSensitivity':
        Range.MinValue = 0.1; Range.MaxValue = 5.0; Range.NudgeValue = 0.1;
        Range.CurrentValue = Settings.touchGyroSensitivity; break;
    case 'uiScale':
        Range.MinValue = 1.0; Range.MaxValue = 1.5; Range.NudgeValue = 0.05;
        Range.CurrentValue = Settings.uiScale; break;
    case 'renderScale':
        Range.MinValue = 0.25; Range.MaxValue = 1.0; Range.NudgeValue = 0.05;
        Range.CurrentValue = Settings.renderScale / 100.0; break;
    default: return false;
    }
    Range.CurrentValue = FClamp(Range.CurrentValue, Range.MinValue, Range.MaxValue);
    // UTUISlider's native caption always formats an integer.
    Range.CurrentValue = Round(Range.CurrentValue * 100.0);
    Range.MinValue *= 100.0;
    Range.MaxValue *= 100.0;
    Range.NudgeValue *= 100.0;
    return true;
}

event bool GetFieldValue(string FieldName, out UIProviderScriptFieldValue FieldValue, optional int ArrayIndex=INDEX_NONE)
{
    local name Field;
    EnsureSettings();
    Field = name(FieldName);
    if (ReadRange(Field, FieldValue.RangeValue))
    {
        FieldValue.PropertyType = DATATYPE_RangeProperty;
        return true;
    }
    FieldValue.PropertyType = DATATYPE_Property;
    switch (Field)
    {
    case 'touchLeftHanded': FieldValue.StringValue = Settings.touchLeftHanded ? "1" : "0"; return true;
    case 'touchInvertY': FieldValue.StringValue = Settings.touchInvertY ? "1" : "0"; return true;
    case 'touchWasd': FieldValue.StringValue = Settings.touchWasd ? "1" : "0"; return true;
    }
    return false;
}

event bool SetFieldValue(string FieldName, const out UIProviderScriptFieldValue FieldValue, optional int ArrayIndex=INDEX_NONE)
{
    local name Field;
    local UIRangeData Range;
    local float Value;
    local bool Enabled;
    EnsureSettings();
    Field = name(FieldName);
    if (ReadRange(Field, Range))
    {
        Value = FieldValue.RangeValue.CurrentValue;
        if (Value != Value)
            return false;
        Value = FClamp(Value, Range.MinValue, Range.MaxValue) / 100.0;
        switch (Field)
        {
        case 'touchSensitivity': Settings.touchSensitivity = Value; break;
        case 'touchAltSensitivity': Settings.touchAltSensitivity = Value; break;
        case 'touchScale': Settings.touchScale = Value; break;
        case 'touchOpacity': Settings.touchOpacity = Value; break;
        case 'touchGyroSensitivity': Settings.touchGyroSensitivity = Value; break;
        case 'uiScale': Settings.uiScale = Value; break;
        case 'renderScale': Settings.renderScale = Clamp(Round(Value * 100.0), 25, 100); break;
        }
    }
    else
    {
        Enabled = FieldValue.StringValue == "1" || FieldValue.StringValue ~= "true";
        switch (Field)
        {
        case 'touchLeftHanded': Settings.touchLeftHanded = Enabled; break;
        case 'touchInvertY': Settings.touchInvertY = Enabled; break;
        case 'touchWasd': Settings.touchWasd = Enabled; break;
        default: return false;
        }
    }
    Settings.SaveConfig();
    return true;
}

defaultproperties
{
    Tag=AndroidPortValues
    WriteAccessType=ACCESS_WriteAll
}
