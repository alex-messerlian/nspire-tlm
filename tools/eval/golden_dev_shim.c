/* One symbol, so the forward-pass golden can link on-device without src/nspire_main.c.
 *
 * g_nspire_log is defined in nspire_main.c, which carries the app's own main(). The golden has its
 * own main, so that file cannot be linked; only the symbol is needed. NULL is the documented
 * "screen only, no log file" state that runq_nspire.c already handles.
 */
#include <stdio.h>
FILE *g_nspire_log = NULL;
