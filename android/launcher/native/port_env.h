/* What the launcher hands to the game's native code: Launch.attach() (Java, in the game's process)
 * sets these environment variables before the game's activity starts native code, so the host
 * reads them with getenv(): no JNI functions, whatever the game's Java package is.
 *
 *   PORT_DISTRIBUTION        the distribution's id from port.json ("toe", "ut3", ...)
 *   PORT_VERSION_NAME        the port's versionName, e.g. "0.7.0-touch"
 *   PORT_VERSION_CODE        the port's versionCode
 *   PORT_LANGUAGE            the launcher's language: en, ru, de or pl
 *   PORT_RENDERER            the renderer id the launcher chose (absent when started without it)
 *   PORT_SKIP_INTRO          "1" or "0" (only for distributions with an intro in port.json)
 *   PORT_PART_<ID>           a part's folder, e.g. PORT_PART_GAME=/storage/emulated/0/UT3/game
 *   PORT_MOUNT_<ID>          a part played from a stored ZIP: its index (zipmount.h); PORT_PART_<ID>
 *                            is then the folder of the archive's few unpacked (compressed) files
 *   PORT_MOUNT_FD_<ID>       that archive's open descriptor, owned by the process
 *   PORT_OPTION_<KEY>        an option of port.json ("1"/"0" for switches)
 *   PORT_TURNIP_DRIVER       the player's own Turnip driver (TurnipDriver.java): the library's full
 *                            path, for adrenotools in place of the port's; empty for the port's own
 *   PORT_UPDATE_VERSION      a newer port release the launcher found (its versionName), else absent
 *   PORT_UPDATE_PAGE         its release page
 *
 * Ids and keys are upper-cased. */
#ifndef PORT_ENV_H
#define PORT_ENV_H

#include <stdlib.h>
#include <string.h>

static inline const char *port_env(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return value && *value ? value : fallback;
}

static inline int port_env_flag(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !*value) return fallback;
    return strcmp(value, "0") != 0 && strcmp(value, "false") != 0;
}

#endif
