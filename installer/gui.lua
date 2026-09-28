-- ChatTLM Setup -- the screen a student sees on a calculator that has never run ChatTLM.
--
-- This file is a REPLACEMENT for Ndless's installer GUI and is a modified MPL 1.1 file; it stays
-- under MPL 1.1 and its source ships in this directory. installer.lua and ipc.lua are Ndless's,
-- unmodified. stage0.S differs on ONE line -- the path it loads the support file from --
-- and tools/eval/gate_installer_exploit.py fails if any other line of it ever differs.
--
-- WHY IT IS A REWRITE AND NOT AN EDIT. The first attempt kept Ndless's renderer and changed only
-- the strings, and the result was reported as "identical to what the original was ... I still see
-- all their original stuff ... their whatever weird code looking UI". That was fair: the matrix
-- rain, the bitmap-font boxes and the scrolling credits marquee ARE the UI, and the words are a
-- small part of it. This draws ChatTLM's own screen instead, with the app's palette.
--
-- ATTRIBUTION LIVES IN THE SOURCE, NOT ON THE SCREEN. MPL 1.1 binds notices in the source of a
-- modified file; it does not require a credit line in a product's UI. This header, installer/
-- README.md and resources/README.md carry it in full. The screen is ChatTLM's.

platform.apilevel = "1.0"

-----------------
-- Device Type --
-----------------

function getDeviceType()
    local _, err = math.eval("DrawLine")
    local cx2 = err == 930

    if device_override and not cx2 then
        return device_override
    end

    return cx2 and "cx2" or "unknown"
end

---------
-- IPC --
---------

-- ZZZ_IPC_LUA_ZZZ

-------------
-- PALETTE --
-------------
-- src/store/app.c PAL_LIGHT, so the setup screen and the app are one product rather than two.
local INK   = {0x0B, 0x0B, 0x0B}     -- P_INK
local INK2  = {0x5B, 0x5B, 0x5B}     -- P_INK2
local INK3  = {0x8F, 0x8F, 0x8F}     -- P_INK3
local LINE  = {0xE5, 0xE5, 0xE5}     -- P_LINE
local CARD  = {0xF4, 0xF4, 0xF4}     -- P_BUBBLE
local BG    = {0xFF, 0xFF, 0xFF}     -- P_BG
local OKFG  = {0x18, 0x6A, 0x3B}     -- P_RESFG
local ERFG  = {0xA8, 0x34, 0x2C}     -- P_ERRFG

local function col(gc, c) gc:setColorRGB(c[1], c[2], c[3]) end

local function text(gc, s, x, y, c, size, style)
    gc:setFont("sansserif", style or "r", size or 10)
    col(gc, c)
    gc:drawString(s, x, y, "top")
end

local function centre(gc, s, y, c, size, style)
    gc:setFont("sansserif", style or "r", size or 10)
    local w = gc:getStringWidth(s)
    col(gc, c)
    gc:drawString(s, (320 - w) / 2, y, "top")
end

-----------
-- STATE --
-----------
local status = "ready"
local failed = nil
local cxii   = false
local tick   = 0
local install_ts = 0

-- Body copy per state. Kept as data so the screen is one layout rather than four.
local BODY = {
    -- THE RESTART IS EXPECTED AND THE SCREEN SAYS SO BEFORE IT HAPPENS.
    --
    -- Measured on hardware: three attempts, two restarted the calculator, the third installed.
    -- That is Ndless's own documented behaviour -- its README says "if it fails or reboots, try
    -- again until it works", twice -- and our exploit files are byte-identical to theirs, so it is
    -- not something we introduced. What we CAN fix is that an unexplained reset reads as a broken
    -- product. Warned about in advance it reads as a retry, which is what it is.
    -- A135. SIX LINES DID NOT FIT. Photographed on device: the last line was cut off mid-height
    -- by the action card, and the footer was cut off by the bottom of the Lua pane. The pane is
    -- shorter than the 240 px screen -- the document tab bar takes the top -- so the usable area
    -- is about 212 px, not 240. Four lines at 15 px from y=78 ends at 138, clear of the card
    -- at 152.
    -- FIVE LINES, NOT SIX. Six forced the auto-shrink below to 11 px spacing, which fits the pane
    -- and is unreadable -- caught by gate_installer_lua, which grew that case because a control
    -- survived the bounds check.
    ready = {
        "This installs the support files",
        "ChatTLM needs in order to run.",
        "",
        "The calculator may reset itself.",
        "If it does, open this again.",
    },
    installing = {
        "Installing. This takes a moment",
        "and the calculator may reset.",
    },
    -- SETUP NO LONGER STARTS THE APP (2026-09-27). Started from inside this document, ChatTLM could
    -- not get the model's memory while the document was still open, and quitting it came back
    -- here. Setup now installs and closes, the home screen says "ChatTLM is ready", and the student
    -- opens ChatTLM from My Documents, as with Ndless itself.
    install_done = {
        "Installed. Open ChatTLM from",
        "the chattlm folder in",
        "My Documents.",
    },
}
local FAIL_BODY = {
    -- THE VERSION NOTE, on our own screen and in plain words rather than an error code.
    os_invalid = {
        "This build supports OS 6.2.0 to",
        "6.4.0 only. A newer OS needs a",
        "newer ChatTLM release.",
    },
    no_resources = {
        "chattlm_support.tns is missing",
        "from the chattlm folder. Copy",
        "the whole folder across again.",
    },
    setup = {
        "Setup could not start. Press",
        "reset on the back, then open",
        "this document again.",
    },
}

function on.paint(gc)
    col(gc, BG)
    gc:fillRect(0, 0, 320, 240)

    -- Header: the product name, then a rule. Same proportions as the app's own title row.
    text(gc, "ChatTLM", 18, 20, INK, 16, "b")
    col(gc, LINE)
    gc:fillRect(18, 44, 284, 1)

    local heading, hcol
    if status == "ready" then
        -- NOT "One-time setup": the loader is gone after every reset, so Setup is run again each time.
        heading, hcol = "Run after every reset", INK2
    elseif status == "install_done" then
        heading, hcol = "Ready", OKFG
    elseif status == "install_failed" then
        heading, hcol = "Did not install", ERFG
    else
        heading, hcol = "Setting up", INK2
    end
    text(gc, heading, 18, 54, hcol, 11, "b")

    -- Body
    local body
    if status == "install_failed" then
        body = FAIL_BODY[failed] or FAIL_BODY.setup
    elseif status == "install_start" or status == "install_requested" then
        body = BODY.installing
    else
        body = BODY[status] or BODY.ready
    end

    -- A135. THE LAYOUT IS COMPUTED, NOT HARDCODED, because the hardcoded one was wrong on device.
    --
    -- Photographed: the sixth body line was half-hidden behind the action card and the footer was
    -- cut off by the bottom of the pane. The Lua pane is NOT the full 240 px screen -- the
    -- document tab bar takes the top -- so roughly 212 px is usable and 78 + 6*15 = 168 ran past
    -- the card at 152 with nothing on screen admitting it. Same silent-truncation class this
    -- project has now hit in the session list, the search sheet and here.
    --
    -- So: the card is placed from the bottom, the body is given the space above it, and the line
    -- height tightens if the body is tall. A body that still would not fit is reported rather
    -- than drawn through.
    local FOOT_Y, CARD_H = 186, 30
    local cy   = FOOT_Y - 8 - CARD_H          -- 148
    local y    = 76
    if not cxii and status == "ready" then
        body = { "ChatTLM needs a TI-Nspire CX II.", "This calculator is not supported." }
    end
    local lh = 15
    if #body * lh > (cy - 4) - y then lh = math.floor(((cy - 4) - y) / #body) end
    for i = 1, #body do
        text(gc, body[i], 18, y, INK2, 10)
        y = y + lh
    end

    -- The action card. On `ready` it is the prompt; while installing it is a progress bar.
    col(gc, CARD)
    gc:fillRect(18, cy, 284, 30)
    col(gc, LINE)
    gc:drawRect(18, cy, 284, 30)

    if status == "ready" then
        if cxii then
            -- A135. IT SAID "Press any key to begin" AND KEYS DID NOT WORK. Reported from the
            -- device: pressing keys did nothing, clicking the card did. The document does not
            -- hold keyboard focus until something is clicked, so on.charIn never fires first --
            -- which makes the label an instruction that cannot be followed. The key handlers stay
            -- wired (they work once focused), but the card now says the thing that always works.
            centre(gc, "Click here to begin", cy + 9, INK, 11, "b")
        else
            centre(gc, "Nothing to do", cy + 9, INK3, 11, "b")
        end
    elseif status == "install_start" or status == "install_requested" then
        -- A bar that moves, so a long step does not read as a freeze.
        local w = 264
        local p = (tick % 40) / 40
        col(gc, LINE)
        gc:fillRect(28, cy + 13, w, 4)
        col(gc, INK2)
        gc:fillRect(28 + p * (w - 60), cy + 13, 60, 4)
    elseif status == "install_done" then
        centre(gc, "Now open ChatTLM", cy + 9, OKFG, 11, "b")
    else
        centre(gc, "Open this document again", cy + 9, ERFG, 11, "b")
    end

    -- Footer: the supported range, always visible. At 202 it fell off the bottom of the pane.
    text(gc, "Built for OS 6.2.0 - 6.4.0", 18, FOOT_Y, INK3, 9)
end

function on.timer()
    ipc_tick()
    tick = tick + 1

    if status == "install_start" and tick > 4 then
        status     = "install_requested"
        install_ts = timer.getMilliSecCounter()
        if cxii then
            ipc_send("install_start")
        else
            ipc_send("install_start", "cx")
        end
    end

    if status == "install_requested"
       and (timer.getMilliSecCounter() - install_ts) > 10000 then
        status = "install_failed"
        failed = "setup"
    end

    platform.window:invalidate()
end

function on.charIn(ch)
    if status ~= "ready" then return end
    if not cxii then return end
    status = "install_start"
    tick   = 0
end

on.arrowKey  = on.charIn
on.enterKey  = on.charIn
on.mouseDown = on.charIn
on.returnKey = on.charIn
on.tabKey    = on.charIn

function on.create()
    cxii = getDeviceType() == "cx2"
    timer.start(0.08)

    ipc_subscribe("install_done", function (d)
        status = d
    end)

    ipc_subscribe("install_failed", function (f, reason)
        status = f
        failed = reason
    end)
end
