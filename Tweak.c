// AlwaysOnDisplay16 0.1.2
//
// iOS 16+:  makes MobileGestalt report the Always-On Display capability as
//           supported (MGGetBoolAnswer), so SpringBoard and Settings treat the
//           device as AOD-capable.
// iOS < 16: native AOD does not exist, so nothing is hooked. See "Porting the
//           AOD" in README.md.
//
// Known unknowns (could not be verified without a device):
//   1. The MobileGestalt key below is the obfuscated AOD name as remembered
//      from public tooling; it may not exist on iOS 16 at all.
//   2. SpringBoard may not ask MobileGestalt about AOD in the way assumed here,
//      and the decision may be made in another process (backlightd).
// Key logging mode (see README) records every key those processes ask for, so
// the real key can be identified from data instead of guessed.
//
// Control files (create/delete them with Filza), all in /var/mobile:
//   aod16_disable  tweak does nothing while this exists
//   aod16_log      log every MobileGestalt key to Documents/AOD16-keys.txt
//   aod16_dump     dump AOD/Backlight related classes to Documents/AOD16-runtime.txt

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <objc/runtime.h>
#include <os/log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <substrate.h>
#include <unistd.h>

// kCFCoreFoundationVersionNumber is ~1856 on iOS 15.x and ~1940+ on iOS 16.
#define CF_VERSION_IOS16 1900.0

#define DISABLE_FLAG_PATH "/var/mobile/aod16_disable"
#define LOG_FLAG_PATH     "/var/mobile/aod16_log"
#define DUMP_FLAG_PATH    "/var/mobile/aod16_dump"
#define LOADING_MARKER    "/var/mobile/aod16_loading_springboard"

#define KEYLOG_OUTPUT_PATH "/var/mobile/Documents/AOD16-keys.txt"
#define DUMP_OUTPUT_PATH   "/var/mobile/Documents/AOD16-runtime.txt"

// Crash guard: SpringBoard must fail to finish loading this many times in a
// row before the tweak disables itself.
#define MAX_CRASH_STRIKES 3

static const char *const kAODKeys[] = {
    "2OOJf1VhVF5eYFSxQ7SWYw",   // unverified, see "Known unknowns"
    NULL
};

static bool gApplyOverride = false;  // answer YES for the AOD key
static bool gLogKeys = false;        // record every key that is asked for
static FILE *gLogFile = NULL;

static bool fileExists(const char *path) {
    return access(path, F_OK) == 0;
}

static void touchFile(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) fclose(f);
}

static bool processIs(const char *name) {
    const char *p = getprogname();
    return p && strcmp(p, name) == 0;
}

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

// ---------------------------------------------------------------------------
// Key logging
// ---------------------------------------------------------------------------

#define MAX_LOGGED_KEYS 4096
static char *gSeen[MAX_LOGGED_KEYS];
static unsigned gSeenCount = 0;
static pthread_mutex_t gLogLock = PTHREAD_MUTEX_INITIALIZER;

static void logKey(CFStringRef key, const char *kind, int value, bool matched) {
    if (!key || !gLogFile) return;

    char name[128];
    if (!CFStringGetCString(key, name, sizeof(name), kCFStringEncodingUTF8)) return;

    char id[160];
    snprintf(id, sizeof(id), "%s|%s", kind, name);

    pthread_mutex_lock(&gLogLock);
    bool seen = false;
    for (unsigned i = 0; i < gSeenCount; i++) {
        if (strcmp(gSeen[i], id) == 0) { seen = true; break; }
    }
    if (!seen && gSeenCount < MAX_LOGGED_KEYS) {
        gSeen[gSeenCount++] = strdup(id);
        fprintf(gLogFile, "%s %s key=%s value=%d matched=%d\n",
                getprogname(), kind, name, value, matched ? 1 : 0);
        fflush(gLogFile);
    }
    pthread_mutex_unlock(&gLogLock);
}

static void openKeyLog(void) {
    gLogFile = fopen(KEYLOG_OUTPUT_PATH, "a");
    if (gLogFile) {
        fprintf(gLogFile, "--- %s pid=%d ---\n", getprogname(), getpid());
        fflush(gLogFile);
    }
}

// ---------------------------------------------------------------------------
// MobileGestalt hooks
// ---------------------------------------------------------------------------

static bool (*orig_MGGetBoolAnswer)(CFStringRef key);
static bool new_MGGetBoolAnswer(CFStringRef key) {
    bool matched = isAODKey(key);
    bool result = (gApplyOverride && matched) ? true : orig_MGGetBoolAnswer(key);
    if (gLogKeys) logKey(key, "bool", result ? 1 : 0, matched);
    return result;
}

// Only installed in key logging mode, and only observes (never changes answers).
static CFTypeRef (*orig_MGCopyAnswer)(CFStringRef key);
static CFTypeRef new_MGCopyAnswer(CFStringRef key) {
    CFTypeRef result = orig_MGCopyAnswer(key);
    if (gLogKeys) logKey(key, "copy", -1, isAODKey(key));
    return result;
}

static void installMobileGestaltHooks(void) {
    void *mg = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_LAZY);
    void *handle = mg ? mg : RTLD_DEFAULT;

    void *boolSym = dlsym(handle, "MGGetBoolAnswer");
    void *copySym = gLogKeys ? dlsym(handle, "MGCopyAnswer") : NULL;

    if (boolSym) {
        MSHookFunction(boolSym, (void *)new_MGGetBoolAnswer, (void **)&orig_MGGetBoolAnswer);
    }
    if (copySym) {
        MSHookFunction(copySym, (void *)new_MGCopyAnswer, (void **)&orig_MGCopyAnswer);
    }

    os_log(OS_LOG_DEFAULT,
           "[AlwaysOnDisplay16] %s: bool hook=%d copy hook=%d override=%d logging=%d",
           getprogname(), boolSym != NULL, copySym != NULL, gApplyOverride, gLogKeys);
}

// ---------------------------------------------------------------------------
// Crash guard (SpringBoard only)
// ---------------------------------------------------------------------------

// Returns true if the AOD override may be applied in this process.
static bool crashGuardAllowsOverride(void) {
    if (fileExists(DISABLE_FLAG_PATH)) {
        os_log(OS_LOG_DEFAULT, "[AlwaysOnDisplay16] disabled by %s", DISABLE_FLAG_PATH);
        return false;
    }

    // Other processes (e.g. Settings) are routinely closed or killed within
    // seconds of launch, so only SpringBoard takes part in crash counting.
    if (!processIs("SpringBoard")) return true;

    int strikes = 0;
    FILE *f = fopen(LOADING_MARKER, "r");
    if (f) {
        if (fscanf(f, "%d", &strikes) != 1) strikes = 1;
        fclose(f);
    }

    if (strikes >= MAX_CRASH_STRIKES) {
        touchFile(DISABLE_FLAG_PATH);
        unlink(LOADING_MARKER);
        os_log(OS_LOG_DEFAULT, "[AlwaysOnDisplay16] %d failed loads in a row, tweak disabled", strikes);
        return false;
    }

    f = fopen(LOADING_MARKER, "w");
    if (f) {
        fprintf(f, "%d", strikes + 1);
        fclose(f);
    }

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        unlink(LOADING_MARKER);
    });
    return true;
}

// ---------------------------------------------------------------------------
// Runtime class dump (SpringBoard only): lists every Objective-C class and its
// methods whose name suggests it takes part in AOD / backlight handling.
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
    if (!processIs("SpringBoard") || !fileExists(DUMP_FLAG_PATH)) return;

    // Wait so SpringBoard has finished loading its frameworks.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC),
                   dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        dumpAODRuntime();
    });
}

__attribute__((constructor))
static void AlwaysOnDisplay16_init(void) {
    if (kCFCoreFoundationVersionNumber < CF_VERSION_IOS16) {
        os_log(OS_LOG_DEFAULT,
               "[AlwaysOnDisplay16] iOS < 16: no native AOD, nothing hooked (fallback not implemented yet)");
        return;
    }

    gLogKeys = fileExists(LOG_FLAG_PATH);
    if (gLogKeys) openKeyLog();

    // backlightd is only observed (key logging), never modified.
    if (processIs("backlightd")) {
        if (gLogKeys) installMobileGestaltHooks();
        return;
    }

    gApplyOverride = crashGuardAllowsOverride();
    if (gApplyOverride || gLogKeys) {
        installMobileGestaltHooks();
    }

    scheduleDumpIfRequested();
}
