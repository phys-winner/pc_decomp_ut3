class AndroidPortOptionsFocusFixture extends AndroidPortViewportClient;

var UTUIOptionList ObservedOptions;
var int SliderNotifications;

function KeepPortFocusLit(UTUIOptionList Options)
{
    ObservedOptions = Options;
}

function SliderChanged(UIObject Sender, int PlayerIndex)
{
    ++SliderNotifications;
}
