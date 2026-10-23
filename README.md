# AlwaysOnDisplay16

A jailbreak tweak (Theos project) that makes iOS 16 treat your iPhone as
Always-On Display capable. It hooks `MGGetBoolAnswer` / `MGCopyAnswer` in
SpringBoard and Settings and answers YES for the AOD capability keys.

This is source, not a prebuilt `.deb`. It has not been compiled or tested on a
device. Build it with Theos on a Mac or Linux machine.

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

The `.deb` lands in `packages/`.

## Install

Open the `.deb` with Filza or Sileo, or from a shell on the device:

```sh
dpkg -i com.example.alwaysondisplay16_0.1.0_*.deb
killall -9 SpringBoard
```

Then check Settings > Display & Brightness for an "Always On Display" toggle.

## Notes and limits

- Capability unlock only. iOS 16 also depends on display hardware (LTPO,
  ProMotion). On other panels the screen may stay at full refresh while
  "always on", which costs battery. Expect a noticeable drain on LCD devices.
- If the toggle doesn't appear, the key strings in `Tweak.c` may not match
  your device. Dump your `com.apple.MobileGestalt.plist` and compare.
- Logs: `os_log` subsystem output tagged `[AlwaysOnDisplay16]` (Console.app or
  `log stream` over USB) shows whether the hooks were installed.
- Bootloop recovery: boot into your jailbreak's safe mode and remove the
  package, or delete `AlwaysOnDisplay16.dylib` and its `.plist` from the
  tweak injection folder.

## Porting the AOD to older iOS (pre-16)

You can't copy Apple's AOD out of iOS 16 and drop it into iOS 15: it is
compiled code tied to iOS 16 frameworks. What you can do is use your iOS 16
device as a reference and rewrite the behavior as the fallback for older iOS.

1. Install this tweak on the iOS 16 device (the iPhone 8) and respring.
2. Trigger the runtime dump by creating the flag file, then respring again:

```sh
touch /var/mobile/.aod16_dump
killall -9 SpringBoard
```

3. After about 30 seconds, `/var/mobile/Documents/AOD16-runtime.txt` lists
   every loaded class with AlwaysOn / Backlight / AOD / BLS in its name,
   with its superclass, source image and methods. Copy it to your computer.
4. Delete the flag file when done: `rm /var/mobile/.aod16_dump`.
5. For deeper inspection, extract the dyld shared cache from the iOS 16 IPSW
   and run class-dump or `ipsw class-dump` on SpringBoard, BacklightServices
   and SpringBoardUIServices. FLEX can inspect the live AOD view hierarchy
   (wallpaper dimming, clock, notification layout) so you can copy the look.
6. Reimplement the behavior in a fallback for `kCFCoreFoundationVersionNumber
   < 1900`: a dimmed overlay window with clock and notifications, plus hooks
   that stop the screen from fully blanking while locked. Hook targets must
   come from the dump of the matching older iOS version, since SpringBoard's
   private classes change between releases.

The pre-16 fallback itself is not implemented in this project yet.

## Uninstall

```sh
dpkg -r com.example.alwaysondisplay16
killall -9 SpringBoard
```
