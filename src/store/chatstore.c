#include <stdio.h>
#include <string.h>
#include "chatstore.h"

#define MAGIC   "TLMCHAT1"
#define TRAILER "ENDTLM"

/* Write `n` bytes preceded by their length, then a newline, so a reader can take exactly the bytes
 * that were written rather than hunting for a delimiter that may appear inside them. */
static int put(FILE *f, const char *s) {
    int n = (int)strlen(s);
    if (fprintf(f, "%d\n", n) < 0) return -1;
    if (n && fwrite(s, 1, (size_t)n, f) != (size_t)n) return -1;
    return fputc('\n', f) == EOF ? -1 : 0;
}
static int get(FILE *f, char *out, int cap) {
    int n = -1;
    if (fscanf(f, "%d", &n) != 1 || n < 0 || n >= cap) return -1;
    if (fgetc(f) == EOF) return -1;                       /* the newline after the count */
    if (n && fread(out, 1, (size_t)n, f) != (size_t)n) return -1;
    out[n] = 0;
    if (fgetc(f) == EOF && n) return -1;                  /* the newline after the payload */
    return n;
}

/* Is the file at `path` one of OURS to overwrite?
 *
 * fopen(path, "wb") truncates before a single byte is checked, so by the time a wrong path is
 * noticed the file it named is already gone. This session that path was the MODEL: a stale pointer
 * left PERSIST naming model4096.bin.tns, and a 7,464,832-byte checkpoint became a 21-byte chat
 * store. The pointer bug is fixed at its source, but a save that can destroy an unrelated file when
 * handed the wrong name is a hazard independent of how it got the wrong name.
 *
 * A missing file is ours to create. An empty one is ours -- a previous write that failed leaves
 * nothing to lose. Anything else must begin with our magic or we do not touch it. */
static int is_ours(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;                                   /* does not exist yet */
    char head[sizeof MAGIC - 1];
    size_t got = fread(head, 1, sizeof head, f);
    fclose(f);
    if (got == 0) return 1;                             /* empty: nothing to destroy */
    if (got != sizeof head) return 0;                   /* too short to be a store */
    return memcmp(head, MAGIC, sizeof head) == 0;
}

int chat_save(const char *path, const app_chat *chats, int n, int cur) {
    /* CHECKED BEFORE THE OPEN, because "wb" is itself the destructive act. */
    if (!is_ours(path)) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int bad = 0;
    if (fprintf(f, "%s\n%d %d\n", MAGIC, n, cur) < 0) bad = 1;
    for (int i = 0; i < n && !bad; i++) {
        const app_chat *c = &chats[i];
        if (fprintf(f, "%d\n", c->nturns) < 0) { bad = 1; break; }
        if (put(f, c->title)) { bad = 1; break; }
        for (int j = 0; j < c->nturns && !bad; j++) {
            const app_turn *t = &c->turn[j];
            if (fprintf(f, "%d\n", t->done) < 0) bad = 1;
            else if (put(f, t->q) || put(f, t->a) || put(f, t->sum)) bad = 1;
        }
    }
    /* The trailer is the proof the write finished. Written LAST, checked FIRST on load. */
    if (!bad && fprintf(f, "%s\n", TRAILER) < 0) bad = 1;
    if (fclose(f) != 0) bad = 1;
    return bad ? -1 : 0;
}

int chat_load(const char *path, app_chat *chats, int max, int *cur) {
    if (cur) *cur = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    /* Trailer first: a file whose last write did not complete is not read at all. */
    char tail[32] = {0};
    if (fseek(f, -(long)(sizeof TRAILER), SEEK_END) != 0 ||
        fread(tail, 1, sizeof TRAILER - 1, f) != sizeof TRAILER - 1 ||
        strncmp(tail, TRAILER, sizeof TRAILER - 1) != 0) { fclose(f); return 0; }
    rewind(f);

    char magic[16] = {0};
    int n = 0, c0 = -1;
    if (fscanf(f, "%15s %d %d", magic, &n, &c0) != 3 || strcmp(magic, MAGIC) != 0 ||
        n < 0 || n > max) { fclose(f); return 0; }
    if (fgetc(f) == EOF && n) { fclose(f); return 0; }

    /* Fill a scratch and only commit on complete success -- "never partially fills" is the contract
     * and a half-restored sidebar is exactly the state that is hard to notice. */
    static app_chat tmp[MAX_CHATS];
    int ok = 1;
    for (int i = 0; i < n && ok; i++) {
        app_chat *ch = &tmp[i];
        memset(ch, 0, sizeof *ch);
        int nt = -1;
        if (fscanf(f, "%d", &nt) != 1 || nt < 0 || nt > MAX_TURNS) { ok = 0; break; }
        if (fgetc(f) == EOF) { ok = 0; break; }
        if (get(f, ch->title, sizeof ch->title) < 0) { ok = 0; break; }
        ch->nturns = nt; ch->used = 1;
        for (int j = 0; j < nt && ok; j++) {
            app_turn *t = &ch->turn[j];
            int done = 0;
            if (fscanf(f, "%d", &done) != 1) { ok = 0; break; }
            if (fgetc(f) == EOF) { ok = 0; break; }
            t->done = done;
            if (get(f, t->q, sizeof t->q) < 0 || get(f, t->a, sizeof t->a) < 0 ||
                get(f, t->sum, sizeof t->sum) < 0) ok = 0;
        }
    }
    fclose(f);
    if (!ok) return 0;
    for (int i = 0; i < n; i++) chats[i] = tmp[i];
    if (cur) *cur = (c0 >= 0 && c0 < n) ? c0 : -1;
    return n;
}
