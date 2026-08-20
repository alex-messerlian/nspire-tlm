/* nsp -- minimal scripted file transfer to a TI-Nspire over USB.
 *
 * Push/pull only, by design. Programs are launched by hand on the device, because every benchmark
 * must run with USB DISCONNECTED -- the CX II drops from 396 MHz to 288 MHz while tethered, so any
 * timing taken over the cable is void. This tool therefore never tries to run anything; the workflow
 * is: nsp push -> human unplugs and runs -> human replugs -> nsp pull.
 *
 * Built on libnspire (lights0123), which is a library with no CLI of its own.
 *
 *   nsp info                       device model, OS version, free storage, battery
 *   nsp ls   <remote-dir>          list a directory
 *   nsp mkdir <remote-dir>         create a directory (parents not created)
 *   nsp push <local> <remote>      upload one file
 *   nsp pull <remote> <local>      download one file
 *   nsp rm   <remote>              delete one file
 *
 * Remote paths are absolute and start with a slash, e.g. /documents/bench/results.txt.tns
 */
/* Not <nspire.h>: that umbrella header includes "usb.h", which does not exist anywhere in the
 * libnspire tree. It is broken upstream. Including the individual headers sidesteps it and pulls in
 * strictly less. */
#include <stdint.h>   /* dir.h uses uint32_t without including this itself */
#include <handle.h>
#include <error.h>
#include <file.h>
#include <dir.h>
#include <devinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static nspire_handle_t *H = NULL;

static int die(const char *what, int err) {
    fprintf(stderr, "nsp: %s: %s\n", what, nspire_strerror(err));
    if (err == NSPIRE_ERR_NODEVICE || err == -NSPIRE_ERR_NODEVICE)
        fprintf(stderr,
            "nsp: no calculator found. Check the cable, and that the device is ON (not just\n"
            "     charging) -- a sleeping Nspire does not enumerate.\n");
    return 2;
}

static int cmd_info(void) {
    struct nspire_devinfo d;
    int e = nspire_device_info(H, &d);
    if (e) return die("device_info", e);
    static const char *batt =  "unknown";
    switch (d.batt.status) {
        case NSPIRE_BATT_POWERED: batt = "external power"; break;
        case NSPIRE_BATT_LOW:     batt = "LOW";            break;
        case NSPIRE_BATT_OK:      batt = "ok";             break;
        default:                  batt = "unknown";        break;
    }
    printf("model         %s\n", d.device_name);
    printf("os            %u.%u.%u\n", d.versions[NSPIRE_VER_OS].major,
           d.versions[NSPIRE_VER_OS].minor, d.versions[NSPIRE_VER_OS].build);
    printf("boot1/boot2   %u.%u.%u / %u.%u.%u\n",
           d.versions[NSPIRE_VER_BOOT1].major, d.versions[NSPIRE_VER_BOOT1].minor,
           d.versions[NSPIRE_VER_BOOT1].build,
           d.versions[NSPIRE_VER_BOOT2].major, d.versions[NSPIRE_VER_BOOT2].minor,
           d.versions[NSPIRE_VER_BOOT2].build);
    printf("storage free  %llu / %llu bytes\n",
           (unsigned long long)d.storage.free, (unsigned long long)d.storage.total);
    printf("ram free      %llu / %llu bytes\n",
           (unsigned long long)d.ram.free, (unsigned long long)d.ram.total);
    printf("battery       %s%s\n", batt, d.batt.is_charging ? " (charging)" : "");
    /* Reported over USB, so this is the TETHERED clock. Expect the 288 MHz figure here, not 396 --
     * it is a cross-check on the USB-clock claim, not a benchmark input. */
    printf("clock_speed   %u  (tethered -- benchmarks run disconnected)\n", d.clock_speed);
    printf("file ext      %s\n", d.extensions.file);
    return 0;
}

static int cmd_ls(const char *path) {
    struct nspire_dir_info *info = NULL;
    int e = nspire_dirlist(H, path, &info);
    if (e) return die(path, e);
    for (uint32_t i = 0; i < info->num; i++) {
        struct nspire_dir_item *it = &info->items[i];
        printf("%s %10u  %s\n", it->type == NSPIRE_DIR ? "d" : "-",
               (unsigned)it->size, it->name);
    }
    printf("(%u entries)\n", info->num);
    nspire_dirlist_free(info);
    return 0;
}

static int cmd_push(const char *local, const char *remote) {
    FILE *f = fopen(local, "rb");
    if (!f) { fprintf(stderr, "nsp: %s: %s\n", local, strerror(errno)); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); fprintf(stderr, "nsp: %s: cannot size\n", local); return 2; }

    void *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf) { fclose(f); fprintf(stderr, "nsp: out of memory for %ld bytes\n", n); return 2; }
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf); fprintf(stderr, "nsp: %s: short read\n", local); return 2;
    }
    fclose(f);

    fprintf(stderr, "push %s -> %s (%ld bytes)... ", local, remote, n);
    fflush(stderr);
    int e = nspire_file_write(H, remote, buf, (size_t)n);
    free(buf);
    if (e) { fprintf(stderr, "\n"); return die(remote, e); }
    fprintf(stderr, "ok\n");
    return 0;
}

static int cmd_pull(const char *remote, const char *local) {
    struct nspire_dir_item attr;
    int e = nspire_attr(H, remote, &attr);
    if (e) return die(remote, e);

    size_t size = attr.size, got = 0;
    void *buf = malloc(size ? size : 1);
    if (!buf) { fprintf(stderr, "nsp: out of memory for %zu bytes\n", size); return 2; }

    e = nspire_file_read(H, remote, buf, size, &got);
    if (e) { free(buf); return die(remote, e); }

    FILE *f = fopen(local, "wb");
    if (!f) { free(buf); fprintf(stderr, "nsp: %s: %s\n", local, strerror(errno)); return 2; }
    if (got && fwrite(buf, 1, got, f) != got) {
        fclose(f); free(buf); fprintf(stderr, "nsp: %s: short write\n", local); return 2;
    }
    fclose(f);
    free(buf);
    fprintf(stderr, "pull %s -> %s (%zu bytes) ok\n", remote, local, got);
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "usage: nsp <command> [args]\n"
        "  info                     device model, OS, storage, battery\n"
        "  ls    <remote-dir>       list a directory\n"
        "  mkdir <remote-dir>       create a directory\n"
        "  push  <local> <remote>   upload one file\n"
        "  pull  <remote> <local>   download one file\n"
        "  rm    <remote>           delete one file\n"
        "\nRemote paths are absolute: /documents/bench/results.txt.tns\n"
        "Programs are NOT launched by this tool -- benchmarks must run with USB disconnected.\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(); return 1; }

    int e = nspire_init(&H);
    if (e) return die("init", e);

    int rc = 1;
    const char *c = argv[1];
    if      (!strcmp(c, "info")  && argc == 2) rc = cmd_info();
    else if (!strcmp(c, "ls")    && argc == 3) rc = cmd_ls(argv[2]);
    else if (!strcmp(c, "push")  && argc == 4) rc = cmd_push(argv[2], argv[3]);
    else if (!strcmp(c, "pull")  && argc == 4) rc = cmd_pull(argv[2], argv[3]);
    else if (!strcmp(c, "mkdir") && argc == 3) {
        e = nspire_dir_create(H, argv[2]);
        rc = e ? die(argv[2], e) : (fprintf(stderr, "mkdir %s ok\n", argv[2]), 0);
    }
    else if (!strcmp(c, "rm")    && argc == 3) {
        e = nspire_file_delete(H, argv[2]);
        rc = e ? die(argv[2], e) : (fprintf(stderr, "rm %s ok\n", argv[2]), 0);
    }
    else usage();

    nspire_free(H);
    return rc;
}
