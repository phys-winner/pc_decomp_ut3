class AndroidPortSettings extends Object config(AndroidPort);

var config float touchSensitivity, touchAltSensitivity, touchScale, touchOpacity;
var config float uiScale;
var config bool touchLeftHanded, touchInvertY, touchWasd;
var config int touchGyro, touchVehicleProfile;
// Gyroscope aim: 0 off, 1 sniper mode (scope zoom, a steered Redeemer), 2 while firing,
// 3 always; -1 until chosen, when an older build's touchGyro (1 secondary fire, 2 always) counts.
var config int touchGyroMode;
var config float touchGyroSensitivity;
var config string renderer, language;
// Render scale: the 3D scene's ScreenPercentage, 25-100; the interface keeps the display's resolution.
var config int fpsLimit, renderScale;

function int GyroMode()
{
    if (touchGyroMode >= 0)
        return Clamp(touchGyroMode, 0, 3);
    return touchGyro == 2 ? 3 : Clamp(touchGyro, 0, 1);
}

defaultproperties
{
    touchSensitivity=0.22
    touchAltSensitivity=1.0
    touchScale=1.0
    uiScale=1.0
    touchOpacity=0.65
    touchGyroMode=-1
    touchGyroSensitivity=1.0
    renderer="gles"
    language="INT"
    fpsLimit=60
    renderScale=100
}
