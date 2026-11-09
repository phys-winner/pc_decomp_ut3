class AndroidPortSettings extends Object config(AndroidPort);

var config float touchSensitivity, touchAltSensitivity, touchScale, touchOpacity;
var config float uiScale;
var config float weaponBarScale, weaponWheelScale;
var config bool touchVisible, touchWeaponSwipe, touchWeaponButtonSwipe, touchWeaponTap, touchStaticStick, touchEditLayout;
var config bool touchLeftHanded, touchInvertY, touchWasd;
var config int touchGyro, touchVehicleProfile;
// Gyroscope aim: 0 off, 1 sniper mode (scope zoom, a steered Redeemer), 2 while firing,
// 3 always, 4 firing or aiming; -1 until chosen, when an older build's touchGyro counts.
var config int touchGyroMode;
var config float touchGyroSensitivity;
var config string renderer, language;
var config int fpsLimit;
var config bool fsr1, antialias;
var config int executionMode;

function int GyroMode()
{
    if (touchGyroMode >= 0)
        return Clamp(touchGyroMode, 0, 4);
    return touchGyro == 2 ? 3 : Clamp(touchGyro, 0, 1);
}

defaultproperties
{
    touchSensitivity=0.22
    touchAltSensitivity=1.0
    touchScale=1.0
    uiScale=1.0
    weaponBarScale=1.0
    weaponWheelScale=1.0
    touchVisible=true
    touchWeaponSwipe=true
    touchWeaponButtonSwipe=true
    touchOpacity=0.65
    touchGyroMode=-1
    touchGyroSensitivity=1.0
    renderer="gles"
    language="INT"
    fpsLimit=60
    executionMode=0
}
