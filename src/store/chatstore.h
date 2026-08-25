#ifndef NS_CHATSTORE_H
#define NS_CHATSTORE_H
#include "app.h"
/* Persist sessions across a run.
 *
 * Sessions were RAM-only: a static array, nothing writing it, so every exit -- by ESC, by the X, by
 * a battery pull -- destroyed every conversation. That was tolerable while exiting was obscure and
 * became a real cost the moment there was a button for it.
 *
 * The format is TEXT with explicit byte counts. Text because the only way to inspect state on this
 * device is to pull the file off it, and a binary blob would need a decoder to answer "did it save
 * anything". Byte counts rather than delimiters because a question can contain any character.
 *
 * A truncated file is REJECTED, not partially loaded: the trailer is checked, and its absence means
 * the last write did not complete. Half a session that looks whole is worse than none. */

/* Returns 0 on success, negative on failure. Never leaves a partial file that would load. */
int chat_save(const char *path, const app_chat *chats, int n, int cur);
/* Returns the number of sessions loaded (0 on a missing, empty or truncated file), and writes the
 * remembered selection into *cur. Never partially fills. */
int chat_load(const char *path, app_chat *chats, int max, int *cur);
#endif
