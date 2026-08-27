/* Sessions survive, deletions stick, and a save cannot destroy a file that is not its own.
 *
 * All three arms come from ONE defect measured on the device. device_app.c builds paths with
 * dpath(), which returns a shared static buffer, and app_set_persist() stored that POINTER. The
 * first send calls rq_build(dpath("model4096.bin.tns")) and rebuilds the buffer, so from then on
 * PERSIST named the model, and persist() opened it with "wb" and wrote the chat store over it.
 *
 * The evidence was sitting in a directory listing for hours: model4096.bin.tns was 21 bytes, and an
 * empty chat store is exactly 21 bytes. It read as a USB truncation because a flaky cable was the
 * story already in progress.
 *
 * Three symptoms, one cause: deletes came back after a restart (they were written to the wrong
 * file), the model would not load (it had been overwritten), and sessions did not persist.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/store/app.c"

static int F;
static void T(const char *n, int ok, const char *d) {
    if (!ok) F++;
    printf("  %s  %-52s %s\n", ok ? "PASS" : "FAIL", n, d ? d : "");
}
static long fsize(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f); return n;
}
/* The exact hazard: one buffer, reused, exactly as dpath() does it. */
static char SHARED[80];
static const char *shared_path(const char *leaf) {
    snprintf(SHARED, sizeof SHARED, "/tmp/tlm_%s", leaf);
    return SHARED;
}

static void seed(int nchats) {
    app_init();
    for (int i = 0; i < nchats; i++) {
        app_chat *c = &CHATS[i];
        memset(c, 0, sizeof *c);
        snprintf(c->title, sizeof c->title, "session %d", i);
        snprintf(c->turn[0].q, sizeof c->turn[0].q, "question %d", i);
        snprintf(c->turn[0].a, sizeof c->turn[0].a, "answer %d", i);
        c->nturns = 1; c->used = 1; c->turn[0].done = 1;
    }
    NCHATS = nchats; CUR = 0;
}

int main(void) {
    char d[200];
    gfx_init();
    printf("test_persist\n");
    const char *CHATS_F = "/tmp/tlm_chats.tns.tns";
    const char *MODEL_F = "/tmp/tlm_model4096.bin.tns";
    remove(CHATS_F); remove(MODEL_F);

    printf("\n  -- THE DEFECT: a shared caller buffer must not steer the save --\n");
    {   /* A stand-in model: big enough that overwriting it is unmistakable. */
        FILE *m = fopen(MODEL_F, "wb");
        for (int i = 0; i < 5000; i++) fputc('M', m);
        fclose(m);
        long before = fsize(MODEL_F);

        seed(2);
        app_set_persist(shared_path("chats.tns.tns"));
        /* Now do what rq_build does: rebuild the SAME buffer with another path. */
        shared_path("model4096.bin.tns");
        persist();

        long after = fsize(MODEL_F);
        snprintf(d, sizeof d, "model %ld -> %ld bytes", before, after);
        T("the model is untouched by a save", after == before, d);
        snprintf(d, sizeof d, "chats file = %ld bytes", fsize(CHATS_F));
        T("and the chat store is the file that was written", fsize(CHATS_F) > 0, d);
    }

    printf("\n  -- THE GUARD: even handed the wrong name, it refuses --\n");
    {   long before = fsize(MODEL_F);
        seed(1);
        int rc = chat_save(MODEL_F, CHATS, NCHATS, CUR);
        long after = fsize(MODEL_F);
        snprintf(d, sizeof d, "rc=%d  %ld -> %ld bytes", rc, before, after);
        T("saving over a foreign file fails", rc != 0, d);
        T("and leaves it byte-for-byte intact", after == before, d);
        /* The positive control: the guard must not refuse its OWN file, or nothing persists at all
         * and every arm below would pass by doing nothing. */
        T("but it still saves to a real chat store",
          chat_save(CHATS_F, CHATS, NCHATS, CUR) == 0, CHATS_F);
        /* A path that does not exist yet is ours to create. */
        remove("/tmp/tlm_new.tns");
        T("and creates a store that is not there yet",
          chat_save("/tmp/tlm_new.tns", CHATS, NCHATS, CUR) == 0, "");
    }

    printf("\n  -- a session survives a restart --\n");
    {   remove(CHATS_F);
        seed(3);
        app_set_persist(CHATS_F);
        persist();
        app_init();                                   /* the restart */
        T("nothing is in memory after a reset", NCHATS == 0, "");
        app_set_persist(CHATS_F);
        snprintf(d, sizeof d, "loaded %d sessions", NCHATS);
        T("all three come back", NCHATS == 3, d);
        T("with their text", strcmp(CHATS[0].turn[0].a, "answer 0") == 0, CHATS[0].turn[0].a);
    }

    printf("\n  -- a DELETION sticks across a restart --\n");
    {   remove(CHATS_F);
        seed(3);
        app_set_persist(CHATS_F);
        persist();
        delete_chat(1);                               /* deletes and persists */
        snprintf(d, sizeof d, "%d in memory", NCHATS);
        T("two remain in memory", NCHATS == 2, d);
        app_init();
        app_set_persist(CHATS_F);
        snprintf(d, sizeof d, "%d after reload", NCHATS);
        T("two after a restart, not three", NCHATS == 2, d);
        /* The one deleted must be GONE, not merely uncounted. */
        int found = 0;
        for (int i = 0; i < NCHATS; i++) if (!strcmp(CHATS[i].title, "session 1")) found = 1;
        T("the deleted session is not among them", !found, "");
    }

    printf("\n  -- deleting EVERYTHING sticks too --\n");
    {   remove(CHATS_F);
        seed(2);
        app_set_persist(CHATS_F);
        persist();
        delete_chat(0); delete_chat(0);
        app_init();
        app_set_persist(CHATS_F);
        snprintf(d, sizeof d, "%d after reload, file %ld bytes", NCHATS, fsize(CHATS_F));
        T("none come back", NCHATS == 0, d);
    }

    printf("\n  -- the feedback path is its own copy too --\n");
    {   /* Same aliasing, one line apart in main(): set_persist then set_feedback both took the
         * shared buffer, so PERSIST named the feedback file before a single chat was saved. */
        remove(CHATS_F);
        seed(1);
        app_set_persist(shared_path("chats.tns.tns"));
        app_set_feedback(shared_path("feedback.tns.tns"));
        persist();
        snprintf(d, sizeof d, "chats %ld bytes", fsize(CHATS_F));
        T("persist still writes the chat store", fsize(CHATS_F) > 0, d);
        T("and feedback keeps its own path",
          FEEDBACK && strstr(FEEDBACK, "feedback") != 0, FEEDBACK ? FEEDBACK : "(null)");
    }

    remove(CHATS_F); remove(MODEL_F); remove("/tmp/tlm_new.tns"); remove("/tmp/tlm_feedback.tns.tns");
    printf("%s\n", F ? "test_persist FAIL" : "test_persist PASS");
    return F ? 1 : 0;
}
