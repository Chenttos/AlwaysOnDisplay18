# AlwaysOnDisplay16 (0.1.2)

A jailbreak tweak (Theos project) that tries to make iOS 16 treat your iPhone
as Always-On Display capable by answering YES for the AOD key in
`MGGetBoolAnswer` (SpringBoard and Settings).

This is source, not a prebuilt `.deb`. It has not been tested on a device, and
two of its assumptions (the key string, and that SpringBoard asks MobileGestalt
about AOD this way) are unverified. v0.1.2 adds a logging mode to find out.

## Build

1. Install Theos: https://theos.dev/docs/installation
2. `export THEOS=~/theos`
3. From this folder:

```sh
# Rootless jailbreaks (Dopamine, palera1n rootless) - default
make package FINALPACKAGE=1

# Rootful jailbreaks (palera1n rootful, checkra1n, unc0ver)
make package FINALPACKAGE=1 THEOS_PACKAGE_SCHEME=
```

The `.deb` lands in `packages/`. Install it with Filza or Sileo, then respring.
Rootless installs the tweak under `/var/jb/Library/MobileSubstrate/DynamicLibraries/`
(you should see both `AlwaysOnDisplay16.dylib` and `AlwaysOnDisplay16.plist` there).

## Control files

Create or delete these empty files in `/var/mobile` with Filza:

| File             | Effect                                                    |
|------------------|-----------------------------------------------------------|
| `aod16_disable`  | Tweak does nothing while it exists                        |
| `aod16_log`      | Log every MobileGestalt key asked for (see below)         |
| `aod16_dump`     | Dump AOD/Backlight related classes (see "Porting")        |

## Diagnosing "nothing happens": key log

1. Create `/var/mobile/aod16_log`, then respring.
2. Wait about 10 seconds, open Settings > Display & Brightness and scroll
   through it, then lock and unlock the phone.
3. Open `/var/mobile/Documents/AOD16-keys.txt` and send it for analysis.
   Each line is `process kind key=<name> value=<0/1> matched=<0/1>`.
   - `matched=1` means the tweak's assumed key was actually asked for.
   - No `matched=1` line means the key string is wrong or unused on iOS 16.
   - Lines from `backlightd` show whether the display daemon is the one
     making the AOD decision.
4. Delete `aod16_log` when done.

## Troubleshooting a respring loop

- The crash guard only counts SpringBoard. If SpringBoard fails to stay up for
  20 seconds three times in a row, the tweak creates `aod16_disable` and stops.
  Quick manual resprings also count as failures, so if the tweak seems dead,
  check for `aod16_disable` and delete it.
- Boot into safe mode and remove the package, or create `aod16_disable`.
- Crash reports (when analytics is on): Settings > Privacy & Security >
  Analytics & Improvements > Analytics Data, entries starting `SpringBoard-`,
  or `/var/mobile/Library/Logs/CrashReporter/`.

## Notes and limits

- Capability unlock only. iOS 16 also depends on display hardware (LTPO,
  ProMotion). On an LCD phone such as an iPhone 8, an always-on screen keeps the
  backlight lit and costs a lot of battery.
- Public docs for MobileGestalt-based AOD unlocking list the feature for
  iOS 18+, so on iOS 16 this approach may not be enough on its own.
- Logs: `os_log` lines tagged `[AlwaysOnDisplay16]` show which hooks installed
  in each process.

## Porting the AOD to older iOS (pre-16)

You can't copy Apple's AOD out of iOS 16 and drop it into iOS 15: it is
compiled code tied to iOS 16 frameworks. Use your iOS 16 device as a reference
and rewrite the behavior as a fallback for older iOS.

1. Create `/var/mobile/aod16_dump` and respring.
2. After about 30 seconds, `/var/mobile/Documents/AOD16-runtime.txt` lists every
   loaded class with AlwaysOn / Backlight / AOD / BLS in its name, with its
   superclass, source image and methods. Delete the flag file afterwards.
3. For deeper inspection, extract the dyld shared cache from the iOS 16 IPSW and
   run class-dump or `ipsw class-dump` on SpringBoard, BacklightServices and
   SpringBoardUIServices. FLEX can inspect the live AOD view hierarchy.
4. Reimplement the behavior for `kCFCoreFoundationVersionNumber < 1900`: a
   dimmed overlay window with clock and notifications, plus hooks that stop the
   screen from fully blanking while locked. Hook targets must come from a dump
   of the matching older iOS version.

The pre-16 fallback itself is not implemented in this project yet.

## Uninstall

```sh
dpkg -r com.example.alwaysondisplay16
killall -9 SpringBoard
```
