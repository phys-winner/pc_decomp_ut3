class AndroidPortDataStore extends UTUIDataStore_StringList;

var AndroidPortSettings Settings;
// Stable engine identifiers; displayed StringData labels may be localized.
var const array<string> LanguageCodes;
// The labels of each StringData list in the game's language (AndroidPortOptions*.ini).
// EStringListData.Strings is localized: the engine never reads it from config.
struct PortChoices
{
    var name Tag;
    var array<string> Labels;
};
var config array<PortChoices> Choices;

function EnsureSettings()
{
    if (Settings == None)
        Settings = new(None) class'AndroidPortSettings';
}

function string Text(string English, string Russian)
{
    EnsureSettings();
    return Caps(Settings.language) == "RUS" ? Russian : English;
}

/** The option widgets show these lists, so the configured labels replace the built-in ones. */
function FillChoices()
{
    local int Index, Field, Label;
    for (Index = 0; Index < Choices.Length; ++Index)
    {
        Field = GetFieldIndex(Choices[Index].Tag);
        if (Field == INDEX_NONE || Choices[Index].Labels.Length == 0)
            continue;
        if (StringData[Field].Strings.Length > 0)
            RemoveStrByIndex(Choices[Index].Tag, 0, StringData[Field].Strings.Length, true);
        for (Label = 0; Label < Choices[Index].Labels.Length; ++Label)
            InsertStr(Choices[Index].Tag, Choices[Index].Labels[Label], Label, true);
    }
}

function PrepareSelections()
{
    local int Index;
    EnsureSettings();
    FillChoices();
    SetCurrentValueIndex('renderer', Settings.renderer ~= "dxvk-turnip" ? 2 : (Settings.renderer ~= "dxvk" ? 1 : 0));
    Index = LanguageCodes.Find(Caps(Settings.language));
    SetCurrentValueIndex('language', Max(0, Index));
    switch (Settings.fpsLimit)
    {
    case 30: Index = 0; break;
    case 45: Index = 1; break;
    case 0: Index = 3; break;
    default: Index = 2; break;
    }
    SetCurrentValueIndex('fpsLimit', Index);
    SetCurrentValueIndex('touchGyro', Settings.GyroMode());
    SetCurrentValueIndex('touchVehicleProfile', Clamp(Settings.touchVehicleProfile, 0, 4));
}

function CommitCollection(name Field)
{
    local int Index;
    local string Value;
    EnsureSettings();
    Index = GetCurrentValueIndex(Field);
    if (Index < 0 || !GetCurrentValue(Field, Value))
        return;
    switch (Field)
    {
    case 'renderer': Settings.renderer = Index == 2 ? "dxvk-turnip" : (Index == 1 ? "dxvk" : "gles"); break;
    case 'language':
        if (Index >= LanguageCodes.Length)
            return;
        Settings.language = LanguageCodes[Index];
        break;
    case 'fpsLimit': Settings.fpsLimit = Index == 3 ? 0 : int(Value); break;
    case 'touchGyro': Settings.touchGyroMode = Clamp(Index, 0, 3); break;
    case 'touchVehicleProfile': Settings.touchVehicleProfile = Clamp(Index, 0, 4); break;
    default: return;
    }
    Settings.SaveConfig();
}

defaultproperties
{
    Tag=AndroidPort
    WriteAccessType=ACCESS_WriteAll
    LanguageCodes(0)="INT"
    LanguageCodes(1)="RUS"
    LanguageCodes(2)="DEU"
    LanguageCodes(3)="FRA"
    LanguageCodes(4)="ITA"
    LanguageCodes(5)="ESN"
    LanguageCodes(6)="CZE"
    LanguageCodes(7)="HUN"
    LanguageCodes(8)="POL"
    StringData(0)=(Tag="renderer",Strings=("OpenGL ES","Vulkan (DXVK)","Vulkan (Turnip)"),DefaultValueIndex=0)
    StringData(1)=(Tag="language",Strings=("English","Russian","German","French","Italian","Spanish","Czech","Hungarian","Polish"),DefaultValueIndex=0)
    StringData(2)=(Tag="fpsLimit",Strings=("30","45","60","Display refresh rate"),DefaultValueIndex=2)
    StringData(3)=(Tag="touchGyro",Strings=("Off","Sniper mode","While firing","Always"),DefaultValueIndex=0)
    StringData(4)=(Tag="touchVehicleProfile",Strings=("On foot","Ground vehicle / walker","Aircraft","Hoverboard","Turret"),DefaultValueIndex=0)
}
