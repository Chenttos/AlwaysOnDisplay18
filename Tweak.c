// AlwaysOnDisplay16
//
// iOS 16+:  makes MobileGestalt report the Always-On Display capability as
//           supported, so SpringBoard and Settings treat the device as
//           AOD-capable.
// iOS < 16: native AOD does not exist, so the MobileGestalt hook does nothing
//           useful. A fallback implementation has to be written against the
//           real AOD classes; see "Porting the AOD" in README.md and use the
//           runtime dump below to collect them from an iOS 16 device.
//
// Injected only into SpringBoard and Preferences (see AlwaysOnDisplay16.plist).
//
// NOTE: the key strings below are MobileGestalt's obfuscated names for the AOD
// capability, taken from public iOS customization tooling. Verify them against
// your own device's com.apple.MobileGestalt.plist if the toggle doesn't appear.

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <os/log.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <substrate.h>
#include <unistd.h>

// kCFCoreFoundationVersionNumber is ~1856 on iOS 15.x and ~1940+ on iOS 16.
#define CF_VERSION_IOS16 1900.0

// Create this empty file on the device to trigger a runtime dump (see README).
#define DUMP_FLAG_PATH   "/var/mobile/.aod16_dump"
#define DUMP_OUTPUT_PATH "/var/mobile/Documents/AOD16-runtime.txt"

static const char *const kAODKeys[] = {
    "2OOJf1VhVF5eYFSxQ7SWYw",
    "j8/Omm6s1lsmTDFsXjsBfA",
    NULL
};

static bool isAODKey(CFStringRef key) {
    if (!key) return false;

    char buf[64];
    if (!CFStringGetCString(key, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        return false;
    }

    for (const char *const *k = kAODKeys; *k; k++) {
        if (strcmp(buf, *k) == 0) return true;
    }
    return false;
}

static bool (*orig_MGGetBoolAnswer)(CFStringRef key);
static bool new_MGGetBoolAnswer(CFStringRef key) {
    if (isAODKey(key)) return true;
    return orig_MGGetBoolAnswer(key);
}

static CFTypeRef (*orig_MGCopyAnswer)(CFStringRef key);
static CFTypeRef new_MGCopyAnswer(CFStringRef key) {
    if (isAODKey(key)) return CFRetain(kCFBooleanTrue);
    return orig_MGCopyAnswer(key);
}

static void installMobileGestaltHooks(void) {
    void *mg = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_LAZY);
    void *handle = mg ? mg : RTLD_DEFAULT;

    void *boolSym = dlsym(handle, "MGGetBoolAnswer");
    void *copySym = dlsym(handle, "MGCopyAnswer");

    if (boolSym) {
        MSHookFunction(boolSym, (void *)new_MGGetBoolAnswer, (void **)&orig_MGGetBoolAnswer);
    }
    if (copySym) {
        MSHookFunction(copySym, (void *)new_MGCopyAnswer, (void **)&orig_MGCopyAnswer);
    }

    os_log(OS_LOG_DEFAULT, "[AlwaysOnDisplay16] MobileGestalt hooks installed (bool=%d copy=%d)",
           boolSym != NULL, copySym != NULL);
}

// ---------------------------------------------------------------------------
// Runtime dump: lists every Objective-C class (and its methods) whose name
// suggests it takes part in AOD / backlight handling. Run it on a device that
// has the real iOS 16 AOD, then use the output to build the pre-16 fallback.
// ---------------------------------------------------------------------------

static bool nameLooksRelevant(const char *name) {
    return strcasestr(name, "AlwaysOn") || strcasestr(name, "Backlight") ||
           strstr(name, "AOD") || strstr(name, "BLS");
}

static void dumpMethods(FILE *f, Class cls, const char *prefix) {
    unsigned int count = 0;
    Method *methods = class_copyMethodList(cls, &count);
    for (unsigned int i = 0; i < count; i++) {
        fprintf(f, "%s %s\n", prefix, sel_getName(method_getName(methods[i])));
    }
    free(methods);
}

static void dumpAODRuntime(void) {
    FILE *f = fopen(DUMP_OUTPUT_PATH, "w");
    if (!f) {
        os_log(OS_LOG_DEFAULT, "[AlwaysOnDisplay16] cannot open dump output");
        return;
    }

    unsigned int classCount = 0;
    Class *classes = objc_copyClassList(&classCount);
    unsigned int written = 0;

    for (unsigned int i = 0; i < classCount; i++) {
        const char *name = class_getName(classes[i]);
        if (!name || !nameLooksRelevant(name)) continue;

        const char *image = class_getImageName(classes[i]);
        Class super = class_getSuperclass(classes[i]);

        fprintf(f, "@interface %s : %s  // %s\n", name,
                super ? class_getName(super) : "(root)", image ? image : "?");
        dumpMethods(f, classes[i], "-");
        dumpMethods(f, object_getClass((id)classes[i]), "+");
        fprintf(f, "@end\n\n");
        written++;
    }

    free(classes);
    fclose(f);
    os_log(OS_LOG_DEFAULT, "[AlwaysOnDisplay16] dumped %u classes to %s", written, DUMP_OUTPUT_PATH);
}

static void scheduleDumpIfRequested(void) {
    if (access(DUMP_FLAG_PATH, F_OK) != 0) return;

    // Wait so SpringBoard has finished loading its frameworks.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        dumpAODRuntime();
    });
}

__attribute__((constructor))
static void AlwaysOnDisplay16_init(void) {
    if (kCFCoreFoundationVersionNumber >= CF_VERSION_IOS16) {
        installMobileGestaltHooks();
    } else {
        os_log(OS_LOG_DEFAULT,
               "[AlwaysOnDisplay16] iOS < 16: no native AOD, MobileGestalt hook skipped (fallback not implemented yet)");
    }

    scheduleDumpIfRequested();
}
