#!/usr/bin/env python3
"""Rebrand a copy of Ndless's loader sources as ChatTLM's support files.

WHY A SCRIPT AND NOT A PATCH. A .patch was the first design and it was lost twice in one session:
regenerating it with `git diff` against a tree that had been restored in between silently produced
an EMPTY patch, and an empty patch applies cleanly. A script cannot fail that way -- every
substitution asserts its expected occurrence count, so if upstream moves, the build STOPS and names
the string, instead of fuzzing or quietly doing nothing.

Run against a COPY (build/resources), never against vendor/Ndless: vendor is a gitignored clone, so
anything edited there is untracked, unreproducible, and lost on the next checkout.

Scope: every string a student can see. Filesystem paths move from ndless/ to chattlm/ as well,
because the folder and filename are the most visible remnant of all -- more visible than any
dialog, since they sit in the calculator's own file browser.

DELIBERATELY NOT CHANGED, and why:
  - the Lua module name "ndless"  -- an API other programs bind to, and invisible on screen.
  - "ndless_resources" in the inner Makefile -- overridden at the genzehn command line instead
    (see resources/Makefile ZEHNFLAGS) so the file on disk keeps no upstream name either.
"""
import sys
from pathlib import Path

# (file, old, new, expected occurrences). The count is the control: a hunk that stops matching
# fails the build loudly rather than being skipped.
EDITS: list[tuple[str, str, str, int]] = [
    # ---------- the banner a student sees the moment the install finishes ----------
    ("install.c", 'unsigned short msg[] = u"Ndless successfully installed!";',
                  'unsigned short msg[] = u"ChatTLM is ready";', 1),
    # limegreen -> PAL_LIGHT OKFG, the same green the app uses
    ("install.c", 'gui_gc_setColor(gc, has_colors ? 0x32cd32 : 0x505050);',
                  'gui_gc_setColor(gc, has_colors ? 0x186A3B : 0x505050);', 1),

    # ---------- opening the support file uninstalls, so say that in our words ----------
    # Button order is load-bearing: this returns 2 for the SECOND button, so the cancel stays
    # second or opening the file by accident would uninstall on a cancel.
    ("install.c",
     'if (show_msgbox_2b("Ndless", "Do you really want to uninstall Ndless r" STRINGIFY(NDLESS_REVISION) "?\\nThe device will reboot.", "Yes", "No") == 2)',
     'if (show_msgbox_2b("ChatTLM", "Remove ChatTLM support files?\\n\\nChatTLM will stop opening until you run ChatTLM Setup again.\\nThe calculator will restart.", "Remove", "Keep") == 2)', 1),

    # ---------- the screen-compatibility dialog ----------
    ("lcd_compat.c",
     '''show_dialog_box2_(0, (const char*) u"Ndless", (const char*) u"Activating compatibility mode.\\n"
                                     "This application hasn't been updated\\n"
                                     "to work with your hardware.\\n"
                                     "You may run into weird issues!", dlg);''',
     '''show_dialog_box2_(0, (const char*) u"ChatTLM", (const char*) u"Adjusting the display for this\\n"
                                     "calculator model.\\n"
                                     "Some of the screen may look wrong\\n"
                                     "until you restart.", dlg);''', 1),

    # ---------- the dialogs shown when a program will not load ----------
    ("zehn_loader.cpp", 'msgbox("Information about the executable", ',
                        'msgbox("About this program", ', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s doesn\'t support CX and CM calculators!"',
                        'msgbox("ChatTLM", "%s needs a TI-Nspire CX II. This calculator is a CX or a CM."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s doesn\'t support clickpads!"',
                        'msgbox("ChatTLM", "%s does not run on clickpad calculators."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s doesn\'t support touchpads!"',
                        'msgbox("ChatTLM", "%s does not run on touchpad calculators."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s requires more than 32MB of RAM!"',
                        'msgbox("ChatTLM", "%s needs more memory than this calculator has."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s requires at least ndless %d.%d.%d!"',
                        'msgbox("ChatTLM", "%s needs newer ChatTLM support files (%d.%d.%d or later).\\nRun ChatTLM Setup again to update them."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s requires ndless %d.%d.%d or older!"',
                        'msgbox("ChatTLM", "%s was built for older ChatTLM support files (%d.%d.%d or earlier)."', 1),
    ("zehn_loader.cpp", 'msgbox("Error", "The application %s requires ndless %d.%d or older!"',
                        'msgbox("ChatTLM", "%s was built for older ChatTLM support files (%d.%d or earlier)."', 1),

    # ---------- the bFLT loader's error dialogs ----------
    ("bflt.c", 'show_msgbox("bFLT loader",err_buffer);', 'show_msgbox("ChatTLM",err_buffer);', 2),

    # ---------- paths: the folder and filenames a student sees in the file browser ----------
    ("ploaderhook.c", '"./ndless/startup"', '"./chattlm/startup"', 1),
    ("persistency.c", '"/documents/ndless/persistent.tns"', '"/documents/chattlm/persistent.tns"', 1),
    ("persistency.c", '"/documents/ndless/currentdoc.tns"', '"/documents/chattlm/currentdoc.tns"', 1),

    # ---------- console-only, but "across the board" was the instruction ----------
    ("ploaderhook.c", 'puts("ndless_load:', 'puts("chattlm_load:', 5),
]


def apply(work: Path) -> int:
    changed = 0
    for fn, old, new, count in EDITS:
        p = work / fn
        s = p.read_text()
        seen = s.count(old)
        if seen == 0 and s.count(new) >= count:
            continue  # already applied; the script is idempotent
        if seen != count:
            sys.exit(f"brand.py: {fn}: expected {count} of {old[:60]!r}, found {seen}.\n"
                     f"  Upstream moved. Fix the edit rather than letting the build skip it.")
        p.write_text(s.replace(old, new))
        changed += count
    return changed


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} <build/resources dir>")
    n = apply(Path(sys.argv[1]))
    print(f"brand.py: {n} substitutions applied")
