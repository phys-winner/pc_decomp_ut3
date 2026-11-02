class AndroidPortOptionsPage extends UTUITabPage_HUDSettings;

// The native HUD page is cloned into this class, so their field sizes must match.

function SetupButtonBar(UTUIButtonBar ButtonBar)
{
    local AndroidPortDataStore Store;
    Store = new(None) class'AndroidPortDataStore';
    ButtonBar.AppendButton(Store.LayoutCaption, OnLayout);
}

function bool OnLayout(UIScreenObject EventObject, int PlayerIndex)
{
    local AndroidPortSettings Settings;
    Settings = new(None) class'AndroidPortSettings';
    Settings.touchEditLayout = true;
    Settings.SaveConfig();
    return true;
}

