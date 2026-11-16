/* Port/eame: the iOS app's host services. iOS ARM64 is the macOS ARM64 port
 * (translated guest memory) on UIKit: SDL owns the application and its scene
 * (SDL_main.h in guest/main.c), the system's GL is OpenGL ES as on Android,
 * and the only folder the player can reach is the app's Documents.
 * Compiled for every Darwin target; empty off iOS. */
#ifdef MEMORIES_IOS
#include "pc/platform/ios.h"
#include "pc/platform/platform.h"
#include "pc/compat/fs.h" /* the port's file-name boundary (check_utf8_paths.py) */
#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* OpenGL ES only: the SDL renderer presents (sdl.c, es_wanted). */
int Platform_HasDesktopGL(void) { return 0; }

int Platform_GuestMemoryHelp(char *why, size_t size)
{
    (void)why;
    (void)size;
    return 0;
}

/* Documents/environment.txt, NAME=value lines: the settings a desktop takes
 * from its environment (MEMORIES_GL_PICTURE=0, MEMORIES_TRACE=...), for a
 * player trying something on a device that has no shell. As android.c's. */
static void read_environment(const char *documents)
{
    char path[1024], line[600];
    FILE *file;
    snprintf(path, sizeof(path), "%s/environment.txt", documents);
    if (!(file = fopen(path, "r"))) return;
    while (fgets(line, sizeof(line), file)) {
        char *equals;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || !(equals = strchr(line, '=')) || equals == line) continue;
        *equals = '\0';
        setenv(line, equals + 1, 1);
        fprintf(stderr, "Port/eame iOS: environment.txt sets %s\n", line);
    }
    fclose(file);
}

void Ios_Startup(void)
{
    const char *home = getenv("HOME");
    char documents[900], path[1024], previous[1024];
    time_t now = time(NULL);
    int log;
    if (!home || !*home) return;
    snprintf(documents, sizeof(documents), "%s/Documents", home);
    mkdir(documents, 0755);
    snprintf(path, sizeof(path), "%s/game", documents);
    mkdir(path, 0755);
    /* Saves, settings, reports and the disc's "game" folder: all in Documents. */
    if (!getenv("MEMORIES_USER_DIR")) setenv("MEMORIES_USER_DIR", documents, 1);
    /* The last run's log is kept: the one worth reading after a crash. */
    snprintf(path, sizeof(path), "%s/port-eame-log.txt", documents);
    snprintf(previous, sizeof(previous), "%s/port-eame-log-previous.txt", documents);
    rename(path, previous);
    log = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log >= 0) {
        fflush(stdout);
        fflush(stderr);
        dup2(log, STDERR_FILENO);
        dup2(log, STDOUT_FILENO);
        close(log);
        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
    fprintf(stderr, "Port/eame iOS: started %s", ctime(&now));
    fprintf(stderr, "Port/eame iOS: documents %s\n", documents);
    if (getcwd(path, sizeof(path))) fprintf(stderr, "Port/eame iOS: bundle %s\n", path);
    read_environment(documents);
    /* As android.c starts the game: the whole screen (the menu bar stays
     * hidden until MENU is tapped, and the touch controls show), and no
     * update check, since an app is not updated by a download. */
    setenv("MEMORIES_FULLSCREEN", "1", 0);
    /* The window is resizable, which SDL takes for "any orientation", and
     * Info.plist's landscape is not read when another app hosts this one.
     * Without this the window was made portrait (1170x2532 on a phone held
     * sideways) and the first seconds were laid out for that, until the
     * rotation's resize arrived. As android.c asks. */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    setenv("MEMORIES_NO_MONITOR", "1", 0);
    setenv("MEMORIES_NO_UPDATE_CHECK", "1", 0);
}
#endif
