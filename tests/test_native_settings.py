"""Execute the port datastore self-check in the original UT3 UnrealScript VM."""

import json
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_uscript


def original_route_contract():
    """Pin the original callbacks/skin restoration reused by the gear route."""
    folder = ROOT / "research/native-settings-source"
    parent = re.sub(r"\s+", "", (folder / "UTUIFrontEnd_Settings.uc").read_text(encoding="utf-8"))
    midgame = re.sub(r"\s+", "", (folder / "UTUITabPage_MidGameSettings.uc").read_text(encoding="utf-8"))
    assert "OpenSceneByName(SettingsPanelsScene,false,OnPanelsSceneOpened)" in parent
    assert "PanelsSceneInst.OnNotifyOptionChanged=OnSettingValueChanged;" in parent
    assert "PanelsSceneInst.OnMarkProfileDirty=MarkDirty;" in parent
    assert "bDirty=Publisher.SaveSubscriberValue(Unused)||bDirty;" in parent
    assert "AdjustSkin(false);S=SceneOwner.OpenSceneByName(class'UTUIFrontEnd_MainMenu'.default.SettingsScene,true);" in midgame
    assert "S.OnSceneDeactivated=AdvancedClosed;" in midgame
    assert "AdjustSkin(true);SettingsList.SetFocus(none);" in midgame
    assert "Skin=bHudSkin?UISkin'UI_InGameHud.UTHUDSkin':UISkin(DynamicLoadObject(\"UI_Skin_Derived.UTDerivedSkin\",class'UISkin'));" in midgame
    assert "if(bActivate){AdvancedOptions();}" in midgame
    source = (ROOT / "android/uscript/AndroidPort/Classes/AndroidPortViewportClient.uc").read_text()
    route = source[source.index("function HandlePortMenuRequest"):source.index("function HandlePortStartupLogin")]
    route = re.sub(r"\s+", "", route)
    assert "MidGameMenu.TabControl.ActivatePage(MidGameSettings,0,true);" in route
    assert "SceneOwner.OpenSceneByName(class'UTUIFrontEnd_MainMenu'.default.SettingsScene)" in route
    assert "UIController.OpenScene(" not in route and "DynamicLoadObject(" not in route
    ingame = route[route.index("if(!class'UIInteraction'.static.IsMenuLevel())"):route.index("SceneOwner=UTUIScene(")]
    assert ingame.index("MidGameMenu.TabControl.ActivatePage(") < ingame.index("PortRequestedPanelPath=PathName(OpenedScene);")
    activation = re.sub(r"\s+", "", source[source.index("function bool ActivateRequestedPortPage"):source.index("function HandlePortMenuRequest")])
    assert "UIController.SceneClient.GetActiveScene()!=Panels" in activation
    assert "!Interaction.ShouldProcessUIInput()" in activation
    return {name: build_uscript.sha(folder / (name + ".uc")) for name in
        ("UTUIFrontEnd_Settings", "UTUITabPage_MidGameSettings")}


def main():
    route_sources = original_route_contract()
    module = ROOT / "build/uscript/AndroidPort.u"
    before = build_uscript.sha(module)
    source = ROOT / "build/uscript-test-source/AndroidPortChecks"
    classes = source / "Classes"
    classes.mkdir(parents=True, exist_ok=True)
    for path in (ROOT / "android/uscript/AndroidPort/Classes").glob("*.uc"):
        shutil.copyfile(path, classes / path.name)
    for path in (ROOT / "tests/uscript/AndroidPortChecks/Classes").glob("*.uc"):
        shutil.copyfile(path, classes / path.name)
    sys.argv = ["build_uscript.py", "--package", "AndroidPortChecks", "--source",
                str(source)]
    build_uscript.main()
    work = ROOT / "build/uscript-compiler"
    engine = work / "UTGame/Config/UTEngine.ini"
    engine.write_text(engine.read_text(encoding="utf-8-sig") +
        "\n[Core.System]\nPaths=..\\UTGame\\Unpublished\\CookedPC\\Script\n" +
        "SeekFreePCPaths=..\\UTGame\\Unpublished\\CookedPC\\Script\n", encoding="utf-8")
    log = work / "UTGame/Logs/AndroidPortChecks.log"
    log.unlink(missing_ok=True)
    command = [str(work / "Binaries/UT3.exe"), "run", "AndroidPortChecks.AndroidPortChecks", "-unattended",
               "-silent", "-nullrhi", "-nosound", "-nop4", "-nohomedir", "-useunpublished", "-log=" + log.name]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    result = subprocess.run(command, cwd=work / "Binaries", stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60, startupinfo=startup,
        creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
    text = log.read_text(encoding="utf-8", errors="replace")
    if result.returncode or "ANDROID_PORT_CHECKS_PASS" not in text or "ANDROID_PORT_CHECK_FAILED" in text:
        print("\n".join(line for line in text.splitlines() if any(token in line for token in
            ("ANDROID_PORT", "Error,", "Warning,", "Critical", "ScriptWarning")))[-6000:])
        raise SystemExit("Native settings commandlet failed: " + str(log))
    assert build_uscript.sha(module) == before, "Production module changed during the isolated test"
    saved = work / "UTGame/Config/UTAndroidPort.ini"
    if not saved.is_file():
        raise AssertionError("SaveConfig did not create UTAndroidPort.ini in the private checkout")
    settings = saved.read_text(encoding="utf-16" if saved.read_bytes().startswith(b"\xff\xfe") else "utf-8-sig")
    assert "[AndroidPortChecks.AndroidPortSettings]" in settings
    for expected in ("renderer=dxvk-turnip", "language=RUS", "fpsLimit=0", "renderScale=25", "touchGyroMode=2",
                     "touchWasd=True", "uiScale=1.250000"):
        assert expected.lower() in settings.lower(), expected
    evidence = ROOT / "research/native-settings-selfcheck"
    evidence.mkdir(exist_ok=True)
    for path in (log, saved, ROOT / "build/uscript/AndroidPortChecks.u", ROOT / "build/uscript/AndroidPortChecks.json"):
        shutil.copyfile(path, evidence / path.name)
    report = {"module_sha256": before, "commandlet_sha256": build_uscript.sha(evidence / "AndroidPortChecks.u"),
              "exit_code": result.returncode, "log": str(evidence / log.name), "log_sha256": build_uscript.sha(log),
              "settings": str(evidence / saved.name), "settings_sha256": build_uscript.sha(saved),
              "result": "PASS", "original_route_sources": route_sources,
              "scope": "Exact production sources compiled into isolated test package in original Windows UE3 VM; original parent callback installation/dirty-delegate dispatch verified. Original skin route source contract checked; live skin/rendering requires Android UI QA"}
    (ROOT / "research/native-settings-selfcheck.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Native settings original UE3 VM and SaveConfig PASS")


if __name__ == "__main__":
    main()
