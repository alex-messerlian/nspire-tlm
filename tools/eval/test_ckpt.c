/* Checkpoint validation, in BOTH directions.
 *
 * This exists because of a measured failure, not a hypothetical one. The model file on the device
 * was 96 bytes. Every check read_checkpoint had -- magic, version, Config, the GS guard -- passed,
 * because 96 bytes of a valid model is a valid HEADER. The loader then mmapped 96 bytes and pointed
 * the weights at data + 256, and the app died on the reader's first send with nothing on screen.
 * The symptom was "pressing enter quits the program".
 *
 * The positive arm alone would be worthless here: a probe that always returns 0 passes it. What
 * makes this a check is the negative arms, and the truncation arm reproduces the exact file that
 * shipped. Per the standing rule, a suite is not trusted until a deliberately broken input fails it.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main

static int F;
static void T(const char *n, int ok, const char *detail) {
    if (!ok) F++;
    printf("  %s  %-46s %s\n", ok ? "PASS" : "FAIL", n, detail ? detail : "");
}

/* The shipped model's own numbers, so the arithmetic is checked against a real artefact rather
 * than against itself. */
static const char *REAL = "build/transfer/model4096.bin.tns";

static long long fsize(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long long n = ftell(f); fclose(f); return n;
}

/* Copy `bytes` of the real model to a temp path, optionally patching one 4-byte field. */
static const char *forge(const char *tag, long long bytes, long off, uint32_t val, int patch) {
    static char out[128];
    snprintf(out, sizeof out, "/tmp/ckpt_%s.bin", tag);
    FILE *in = fopen(REAL, "rb"), *o = fopen(out, "wb");
    if (!in || !o) { if (in) fclose(in); if (o) fclose(o); return NULL; }
    char buf[65536]; long long left = bytes;
    while (left > 0) {
        size_t want = (size_t)(left < (long long)sizeof buf ? left : (long long)sizeof buf);
        size_t got = fread(buf, 1, want, in);
        if (!got) break;
        fwrite(buf, 1, got, o); left -= (long long)got;
    }
    fclose(in);
    if (patch) { fseek(o, off, SEEK_SET); fwrite(&val, 4, 1, o); }
    fclose(o);
    return out;
}

int main(void) {
    char why[160];
    printf("test_ckpt\n");

    long long real = fsize(REAL);
    if (real < 0) {
        /* NOT `return 0`. build/transfer/ is gitignored, so on a fresh clone this printed SKIP and
         * exited clean -- the gate reported PASS having asserted nothing about the loader. That is
         * this project's own rule violated inside the suite that enforces it: "cannot check" and
         * "checked and clean" must never share an exit status. Exit 2, which run_gates.sh already
         * renders as CANNOT CHECK and counts as a failure. */
        printf("  CANNOT CHECK  %s not present -- run: cp build/model4096_gs96.bin %s\n", REAL, REAL);
        return 2;
    }

    /* 1. The derived size reproduces a real artefact exactly. If this drifts, every arm below is
     *    measuring the wrong thing, so it is asserted first. */
    {   FILE *f = fopen(REAL, "rb");
        uint32_t magic; int version, gs; Config c; uint8_t shared;
        if (fread(&magic,4,1,f)!=1 || fread(&version,4,1,f)!=1 || fread(&c,sizeof c,1,f)!=1
            || fread(&shared,1,1,f)!=1 || fread(&gs,4,1,f)!=1) { printf("  FAIL header\n"); return 1; }
        fclose(f);
        long long want = rq_expected_size(&c, shared, gs);
        char d[96]; snprintf(d, sizeof d, "derived=%lld actual=%lld", want, real);
        T("expected_size matches the shipped model", want == real, d);
    }

    /* 2. POSITIVE CONTROL: the real file must be accepted. Without this the probe could reject
     *    everything and all the negative arms would still pass. */
    /* why[] is cleared first: on success rq_probe writes nothing, and printing an uncleared
     * buffer showed the PREVIOUS arm's string here, which reads as a reason for a passing case. */
    why[0] = 0;
    T("the real model probes clean", rq_probe(REAL, why, sizeof why) == 0,
      why[0] ? why : "(no complaint)");

    /* 3. THE SHIPPED BUG, reproduced exactly: 96 bytes, a valid header prefix. */
    {   const char *p = forge("trunc96", 96, 0, 0, 0);
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("96-byte truncation is REJECTED", rc != 0, why);
    }

    /* 4. Truncated one byte short. The off-by-one case a >= comparison would wave through. */
    {   const char *p = forge("short1", real - 1, 0, 0, 0);
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("one byte short is REJECTED", rc != 0, why);
    }

    /* 5. A file LARGER than its Config implies. Config and payload disagree, so the weights would
     *    be read at the wrong offsets -- as wrong as a short file, and a lower-bound check misses
     *    it entirely. */
    {   const char *p = forge("long1", real, 0, 0, 0);
        if (p) { FILE *o = fopen(p, "ab"); if (o) { fputc(0, o); fclose(o); } }
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("one byte long is REJECTED", rc != 0, why);
    }

    /* 6. Header truncated below even the fixed fields. */
    {   const char *p = forge("stub8", 8, 0, 0, 0);
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("8-byte stub is REJECTED", rc != 0, why);
    }

    /* 7. Bad magic at a correct length, so only the magic distinguishes it. */
    {   const char *p = forge("magic", real, 0, 0xDEADBEEF, 1);
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("bad magic is REJECTED at full length", rc != 0, why);
    }

    /* 8. A GS the binary was not compiled for. FIXED_GS is a compile-time divisor here, so a
     *    mismatch is not a quality regression, it is wrong arithmetic. Offset 37: 4 magic +
     *    4 version + 28 Config + 1 shared_classifier. */
    {   const char *p = forge("gs", real, 37, 64, 1);
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("wrong group size is REJECTED", rc != 0, why);
    }

    /* 8b. A143. AN ODD HEAD SIZE. n_heads is Config field 4, at offset 8 + 3*4 = 20. With 32 heads
     *     the shipped dim 352 gives head_size 11. The forward pass would run and pair dimensions
     *     ACROSS heads in RoPE; the probe must refuse it.
     *
     *     The reason is asserted, not just the exit code. Changing n_heads also changes the
     *     expected file size, so the size check would reject this file too -- a test that only
     *     looked at rc would pass with the head-size check deleted, which is exactly the
     *     survived-control shape this repo keeps finding. */
    {   const char *p = forge("oddhead", real, 20, 32, 1);
        why[0] = 0;
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("odd head size is REJECTED, for that reason", rc != 0 && strstr(why, "odd") != NULL, why);
    }
    /*     And a head count that does not divide dim at all (352 / 5). */
    {   const char *p = forge("baddiv", real, 20, 5, 1);
        why[0] = 0;
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("heads not dividing dim is REJECTED, for that reason",
          rc != 0 && strstr(why, "divisible") != NULL, why);
    }

    /*     A151. A row length the group does not divide (hidden_dim 1000 at the file's group): the
     *     engine would skip the remainder of every row. Asserted on the REASON, not only the refusal,
     *     because a changed hidden_dim also breaks the size check -- a test that looked only at rc
     *     would pass with the row check deleted. hidden_dim is the second config int, offset 12. */
    {   const char *p = forge("rowalign", real, 12, 1000, 1);
        why[0] = 0;
        int rc = p ? rq_probe(p, why, sizeof why) : -1;
        T("a row length the group does not divide is REJECTED, for that reason",
          rc != 0 && strstr(why, "row length") != NULL, why);
    }

    /* 9. A missing file must report, not crash. */
    T("absent file is REJECTED", rq_probe("/tmp/definitely_not_here.bin", why, sizeof why) != 0, why);

    /* 10. The reason must be non-empty on every rejection. "Cannot check" and "checked and clean"
     *     must never share an exit status, and a rejection with no words is the same silence this
     *     whole file exists to remove. */
    {   const char *p = forge("trunc96b", 96, 0, 0, 0);
        why[0] = 0; if (p) rq_probe(p, why, sizeof why);
        T("rejection carries a reason", why[0] != 0, why);
    }

    printf("%s\n", F ? "test_ckpt FAIL" : "test_ckpt PASS");
    return F ? 1 : 0;
}
