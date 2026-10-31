"""Build pinned native ARM64 OpenAL Soft with only an AAudio output route.

Pristine upstream sources stay in reference. All changes are byte-recorded
private overlays. --run executes only the known-source CLI audio fixture.
"""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"build/native-audio"
REVISION="b2c48f7718ef3fcf67921a8b6534c4914e328970"
OBOE_REVISION="b115f47593969fd67a21e9f63640ffef749b5067"

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def checked(command,**kwargs):
    p=subprocess.run([str(x)for x in command],capture_output=True,text=True,encoding="utf-8",errors="replace",**kwargs)
    if p.returncode:raise RuntimeError(f"commandfailed {command}:\n{p.stdout[-9000:]}\n{p.stderr[-6000:]}")
    return p.stdout

def upstream(name,revision):
    directory=ROOT/"reference"/name;info=json.loads((directory/"provenance.json").read_text())
    if info["revision"]!=revision:raise ValueError("Upstream revision changed:"+name)
    for relative,digest in info["source_tree_sha256"].items():
        if sha(directory/"source"/relative)!=digest:raise ValueError("Pristine upstream file changed:"+relative)
    return directory,info

def overlay(directory,info,destination):
    for relative,digest in info["source_tree_sha256"].items():
        source=directory/"source"/relative;target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists()or sha(target)!=digest:shutil.copyfile(source,target)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--run",action="store_true");args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);openal,ai=upstream("openal-soft",REVISION);oboe,oi=upstream("oboe",OBOE_REVISION)
    alsource=OUT/"source-openal";osource=OUT/"source-oboe";overlay(openal,ai,alsource);overlay(oboe,oi,osource);patches={}
    def patch(path,old,new,count=1):
        text=path.read_text();actual=text.count(old)
        if actual!=count:raise ValueError(f"Unexpected patchanchor {path}:{actual}/{count}")
        original=sha(path);path.write_text(text.replace(old,new),encoding="utf-8",newline="\n")
        entry=patches.setdefault(str(path.relative_to(OUT)),dict(original_sha256=original,steps=[]))
        entry["steps"].append(dict(input_sha256=original,output_sha256=sha(path),before=old,after=new));entry["patched_sha256"]=sha(path)
    # Strict AAudio: no OpenSL fallback, including on unsupported systems.
    patch(osource/"src/common/AudioStreamBuilder.cpp",'''    if (isAAudioRecommended() && mAudioApi != AudioApi::OpenSLES) {
        stream = new AudioStreamAAudio(*this);
    } else if (isAAudioSupported() && mAudioApi == AudioApi::AAudio) {
        stream = new AudioStreamAAudio(*this);
        LOGE("Creating AAudio stream on 8.0 because it was specified. This is error prone.");
    } else {
        if (getDirection() == oboe::Direction::Output) {
            stream = new AudioOutputStreamOpenSLES(*this);
        } else if (getDirection() == oboe::Direction::Input) {
            stream = new AudioInputStreamOpenSLES(*this);
        }
    }''','''    if (isAAudioSupported() && mAudioApi != AudioApi::OpenSLES) {
        stream = new AudioStreamAAudio(*this);
    } // Frostpunk native runtime: unavailable AAudio is an error, never OpenSL.
''')
    cm=(osource/"CMakeLists.txt").read_text();before=cm
    for name in ["AudioInputStreamOpenSLES","AudioOutputStreamOpenSLES","AudioStreamOpenSLES","EngineOpenSLES","OpenSLESUtilities","OutputMixerOpenSLES"]:
        line=f"    src/opensles/{name}.cpp\n"
        if cm.count(line)!=1:raise ValueError("Oboe source-list anchor changed")
        cm=cm.replace(line,"")
    cm=cm.replace("target_link_libraries(oboe PRIVATE log OpenSLES)","target_link_libraries(oboe PRIVATE log)")
    patch(osource/"CMakeLists.txt",before,cm)
    # Native OpenAL can expose no silent-device route in this private build.
    patch(alsource/"alc/alc.cpp",'    BackendInfo{"null"sv, NullBackendFactory::getFactory},\n',"")
    patch(alsource/"alc/alc.cpp",'''        const auto reversenamerange = backends | std::views::reverse;
        const auto iter = std::ranges::find(reversenamerange, "null"sv, &BackendInfo::name);
        backends = std::span{backends.begin(), iter.base()};''','''        // This Android-only build contains only its physical AAudio backend.
        backends = std::span{BackendList};''')
    cm=(alsource/"CMakeLists.txt").read_text();before=cm;cm=cm.replace("    alc/backends/null.cpp\n","").replace('set(BACKENDS  "${BACKENDS} Null")','# Null backend intentionally absent in the physical-output-only Android build.')
    patch(alsource/"CMakeLists.txt",before,cm)
    patch(alsource/"libopenal.version","global:","global:\n        FrostpunkAudio_ProbeEnable;\n        FrostpunkAudio_GetMetrics;")
    backend=alsource/"alc/backends/oboe.cpp";text=backend.read_text();before=text
    text=text.replace('#include "oboe/Oboe.h"','#include "oboe/Oboe.h"\n#include "metrics.h"')
    text=text.replace('oboe::AudioStreamBuilder{}.setDirection(oboe::Direction::Output)','oboe::AudioStreamBuilder{}.setAudioApi(oboe::AudioApi::AAudio)->setDirection(oboe::Direction::Output)')
    text=text.replace('auto builder = oboe::AudioStreamBuilder{};','auto builder = oboe::AudioStreamBuilder{};\n    builder.setAudioApi(oboe::AudioApi::AAudio);')
    old='''    mDevice->renderSamples(audioData, gsl::narrow_cast<uint32_t>(numFrames),
        gsl::narrow_cast<uint32_t>(oboeStream->getChannelCount()));'''
    new='''    const bool measure = FrostpunkAudio_ProbeEnabled();
    const auto cpuStart = measure ? FrostpunkAudio_Clock(1) : 0;
    const auto wallStart = measure ? FrostpunkAudio_Clock(0) : 0;
    mDevice->renderSamples(audioData, gsl::narrow_cast<uint32_t>(numFrames),
        gsl::narrow_cast<uint32_t>(oboeStream->getChannelCount()));
    if(measure) {
        const auto elapsedCpu = FrostpunkAudio_Clock(1) - cpuStart;
        const auto elapsedWall = FrostpunkAudio_Clock(0) - wallStart;
        const auto xr = oboeStream->getXRunCount();
        FrostpunkAudio_RecordCallback(audioData, numFrames, oboeStream->getChannelCount(),
            static_cast<int>(oboeStream->getFormat()), elapsedCpu, elapsedWall,
            static_cast<int>(oboeStream->getState()), xr ? xr.value() : -1,
            oboeStream->getFramesWritten(), oboeStream->getFramesRead());
    }'''
    if text.count(old)!=1:raise ValueError("Oboe callback anchor changed")
    text=text.replace(old,new)
    old='''    TRACE("Got stream with properties:\\n{}", oboe::convertToText(mStream.get()));'''
    new=old+'''
    if(mStream->getAudioApi() != oboe::AudioApi::AAudio)
        throw al::backend_exception{al::backend_error::DeviceError, "AAudio is required"};
    FrostpunkAudio_RecordStream(static_cast<int>(mStream->getAudioApi()),
        static_cast<int>(mStream->getState()), mStream->getChannelCount(),
        mStream->getSampleRate(), static_cast<int>(mStream->getFormat()),
        mStream->getFramesPerBurst(), static_cast<int>(mStream->getPerformanceMode()),
        static_cast<int>(mStream->getSharingMode()), mStream->getDeviceId());'''
    if text.count(old)!=2:raise ValueError("Oboe stream-property anchors changed")
    text=text.replace(old,new)
    text=text.replace('return type == BackendType::Playback || type == BackendType::Capture;','return type == BackendType::Playback; /* Capture is not exposed by this playback-only port. */')
    patch(backend,before,text)
    cfg=json.loads((ROOT/"port.json").read_text());sdk=Path(cfg["android_sdk"]);ndk=sdk/"ndk"/cfg["android_ndk_version"];cmake=sdk/"cmake/3.22.1/bin/cmake.exe";ninja=cmake.parent/"ninja.exe";compiler=ndk/"toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
    project=OUT/"project";project.mkdir(exist_ok=True);audio=ROOT/"android/audio"
    project_text=f'''cmake_minimum_required(VERSION 3.22.1)
project(FrostpunkNativeAudio LANGUAGES C CXX)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LIBTYPE SHARED CACHE STRING "" FORCE)
set(OBOE_SOURCE "{osource.as_posix()}" CACHE PATH "" FORCE)
add_subdirectory("{alsource.as_posix()}" openal)
target_sources(OpenAL PRIVATE "{(audio/'metrics.cpp').as_posix()}")
target_include_directories(OpenAL PRIVATE "{audio.as_posix()}")
set_source_files_properties("{(audio/'metrics.cpp').as_posix()}" PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-ffp-contract=off")
add_executable(native_audio_probe "{(audio/'native_probe.cpp').as_posix()}")
target_compile_features(native_audio_probe PRIVATE cxx_std_20)
target_include_directories(native_audio_probe PRIVATE "{audio.as_posix()}" "{(alsource/'include').as_posix()}")
target_link_libraries(native_audio_probe PRIVATE OpenAL log)
target_link_options(native_audio_probe PRIVATE "-Wl,-z,max-page-size=16384")
''';(project/"CMakeLists.txt").write_text(project_text)
    options={"ALSOFT_UTILS":"OFF","ALSOFT_EXAMPLES":"OFF","ALSOFT_INSTALL":"OFF","ALSOFT_INSTALL_CONFIG":"OFF","ALSOFT_INSTALL_HRTF_DATA":"OFF","ALSOFT_INSTALL_AMBDEC_PRESETS":"OFF","ALSOFT_INSTALL_EXAMPLES":"OFF","ALSOFT_INSTALL_UTILS":"OFF","ALSOFT_EAX":"OFF","ALSOFT_CPUEXT_NEON":"ON","ALSOFT_REQUIRE_NEON":"ON","ALSOFT_BACKEND_OBOE":"ON","ALSOFT_REQUIRE_OBOE":"ON","ALSOFT_EMBED_HRTF_DATA":"ON","ALSOFT_UPDATE_BUILD_VERSION":"OFF"}
    for name in ["PIPEWIRE","PULSEAUDIO","ALSA","OSS","SOLARIS","SNDIO","WASAPI","DSOUND","WINMM","JACK","COREAUDIO","OPENSL","PORTAUDIO","SDL3","SDL2","WAVE"]:options["ALSOFT_BACKEND_"+name]="OFF"
    build=OUT/"cmake";command=[cmake,"-S",project,"-B",build,"-G","Ninja","-DCMAKE_MAKE_PROGRAM="+str(ninja),"-DCMAKE_TOOLCHAIN_FILE="+str(ndk/"build/cmake/android.toolchain.cmake"),"-DANDROID_ABI=arm64-v8a","-DANDROID_PLATFORM=android-30","-DANDROID_STL=c++_shared","-DCMAKE_BUILD_TYPE=Release",*[f"-D{k}={v}"for k,v in options.items()]]
    own_sources=[Path(__file__),audio/"metrics.h",audio/"metrics.cpp",audio/"native_probe.cpp",openal/"provenance.json",oboe/"provenance.json"]
    own_pins={str(p.relative_to(ROOT)):sha(p)for p in own_sources}
    overlay_pins={str(p.relative_to(OUT)):sha(p)for directory in [alsource,osource]for p in directory.rglob('*')if p.is_file()}
    configure=checked(command);(OUT/"configure.log").write_text(configure);print("Native audio configure PASS; building OpenAL+Oboe AAudio/NEON",flush=True)
    build_stdout=checked([cmake,"--build",build,"--target","OpenAL","native_audio_probe","--parallel","8"]);(OUT/"build.log").write_text(build_stdout)
    library=build/"openal/libopenal.so";probe=build/"native_audio_probe"
    if not library.exists()or not probe.exists():raise RuntimeError("Native audio build outputs missing")
    config=(build/"openal/config_simd.h").read_text()+(build/"openal/config_backends.h").read_text()
    if '#define HAVE_NEON 1'not in config or '#define HAVE_OBOE 1'not in config:raise RuntimeError("Required NEON/Oboe not in compiled configuration")
    readelf=compiler.parent/"llvm-readelf.exe";dynamic=checked([readelf,"-h","-d",library]);(OUT/"ELF.txt").write_text(dynamic)
    if "AArch64"not in dynamic or "OpenSLES"in dynamic:raise RuntimeError("Unexpected native audio ABI/dependency")
    exports=checked([readelf,"--dyn-syms","--wide",library]);(OUT/"exports.txt").write_text(exports)
    for symbol in ["alBufferData","alSourcef","alcOpenDevice","FrostpunkAudio_GetMetrics"]:
        if symbol not in exports:raise RuntimeError("Required native export missing:"+symbol)
    runtime=OUT/"runtime";runtime.mkdir(exist_ok=True);shutil.copyfile(library,runtime/"libopenal.so");strip=compiler.parent/"llvm-strip.exe";checked([strip,"--strip-unneeded",runtime/"libopenal.so"]);stl=ndk/"toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so";shutil.copyfile(stl,runtime/stl.name)
    licenses=OUT/"licenses";licenses.mkdir(exist_ok=True)
    license_inputs={"OpenAL-COPYING.txt":openal/"source/COPYING","OpenAL-PFFFT-LICENSE.txt":openal/"source/LICENSE-pffft","fmt-LICENSE.txt":openal/"source/fmt-11.2.0/LICENSE","GSL-LICENSE.txt":openal/"source/gsl/LICENSE","Oboe-LICENSE.txt":oboe/"source/LICENSE"}
    for name,path in license_inputs.items():shutil.copyfile(path,licenses/name)
    (licenses/"SOURCE-PROVENANCE.txt").write_text("OpenAL Soft1.25.2 "+REVISION+"\nhttps://github.com/kcat/openal-soft/releases/tag/1.25.2\nOboe1.11.0 "+OBOE_REVISION+"\nhttps://github.com/google/oboe/releases/tag/1.11.0\nOriginal source archives, private overlay changes, and exact build recipe are retained in this project's reference/openal-soft, reference/oboe, and tools/build_native_audio.py.\n")
    upstream("openal-soft",REVISION);upstream("oboe",OBOE_REVISION)
    if any(sha(ROOT/name)!=digest for name,digest in own_pins.items())or any(sha(OUT/name)!=digest for name,digest in overlay_pins.items()):raise RuntimeError("Native audio source/overlay changed during build")
    report=dict(timestamp_utc=datetime.now(timezone.utc).isoformat(),source_sha256=own_pins,overlay_source_sha256=overlay_pins,OpenAL=ai,Oboe=oi,overlay_changes=patches,cmake_options=options,compiler_sha256=sha(compiler),configure_command=[str(x)for x in command],build_command=[str(x)for x in [cmake,"--build",build,"--target","OpenAL","native_audio_probe","--parallel","8"]],strip_command=[str(strip),"--strip-unneeded",str(runtime/"libopenal.so")],library_sha256=sha(runtime/"libopenal.so"),library_bytes=(runtime/"libopenal.so").stat().st_size,unstripped_library_bytes=library.stat().st_size,fixture_sha256=sha(probe),runtime_files={p.name:sha(p)for p in runtime.iterdir()if p.is_file()},licenses={p.name:sha(p)for p in licenses.iterdir()if p.is_file()},AArch64=True,NEON=True,AAudio_only=True,capture_exposed=False,null_wave_OpenSL_available=False,native_Win64_redirect_implemented=False,game_boot_proven=False)
    if args.run:
        adb=[sdk/"platform-tools/adb.exe","-s",cfg["device"]];remote="/data/local/tmp/frostpunk/native-audio";checked([*adb,"shell","mkdir","-p",remote])
        for p in [probe,*runtime.iterdir()]:checked([*adb,"push",p,remote+"/"+p.name])
        checked([*adb,"shell","chmod","755",remote+"/native_audio_probe"])
        result=subprocess.run([str(x)for x in [*adb,"shell",f"cd {remote} && LD_LIBRARY_PATH={remote} ALSOFT_LOGLEVEL=3 ALSOFT_DRIVERS=oboe ./native_audio_probe"]],capture_output=True,text=True,timeout=15)
        (OUT/"device-stdout.txt").write_text(result.stdout);(OUT/"device-stderr.txt").write_text(result.stderr)
        report.update(serial=cfg["device"],android_returncode=result.returncode,android_stdout=result.stdout,android_stderr=result.stderr)
        if result.returncode==0:
            actual=json.loads(result.stdout);report.update(android_hardware_pass=actual["success"],android_fixture=actual)
        else:report["android_hardware_pass"]=False
    receipt=OUT/"build-receipt.json";receipt.write_text(json.dumps(report,indent=2)+'\n');(ROOT/("research/native-audio.json"if args.run else "research/native-audio-build.json")).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(receipt=str(receipt),library_bytes=report["library_bytes"],AAudio_only=True,NEON=True,hardware_pass=report.get("android_hardware_pass"),returncode=report.get("android_returncode"))))
    if args.run and not report["android_hardware_pass"]:raise SystemExit(1)

if __name__=="__main__":main()
