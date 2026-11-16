#ifndef MEMORIES_PC_IOS_H
#define MEMORIES_PC_IOS_H
/* Port/eame: the iOS app (ios.c). */

/* First thing in main: the player's folder is the app's Documents folder,
 * which the Files app shows ("On My iPhone"), with a "game" folder in it for
 * the disc image; and what the game prints goes to port-eame-log.txt there,
 * since an app has no console. */
void Ios_Startup(void);

#endif
