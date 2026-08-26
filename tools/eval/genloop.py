"""THE generation loop. One implementation, imported — never rewritten.

THE FINDING, WHICH IS NOT "SOMEONE FORGOT TWICE". There was no importable generation path in this
repo. Twenty files each contained their own copy of "generate until <end>, and when the model emits
</tool>, execute the call and feed <res>...</res> back in" -- because every harness that needed one
had to write one. Bug 5 was not a lapse that recurred; it was a defect that EVERY harness could
reproduce independently, and two of twenty happening to do so is the expected outcome of that
structure rather than bad luck.

That is why the fix is a module and an executable guard rather than a note. Omitting the injection
does not fail loudly — the model emits `</tool>`,
receives nothing to continue from, and loops:

    <tool> eval<arg> (6.626e-34)*(1.0)</tool></tool></tool></tool></tool>...

which scores as a well-formed-failure on every answer item while refusals, needing no tool call,
pass untouched. That pins any adherence metric at exactly the refusal fraction of the eval set and
looks like a real measurement. It has now happened twice in this project: once as Bug 5, recorded in
the project log, and once in train/format_sweep.py in the same session that cited the record.

Documentation did not prevent the second occurrence. This module is the fix that does not depend on
anyone reading anything: there is one loop, callers import it, and tools/eval/test_genloop.py fails
if the shipped path and a harness disagree about what the same model produces.

The loop is deliberately agnostic about the model. It takes a `step` callable returning logits for
the next token, so PyTorch on the host and anything else can share it.
"""
import re

TOOL_RE = re.compile(r"<tool>.*?</tool>", re.S)


def generate(step, encode, decode, prompt, *, res_id, end_id, toolc_id, run_tool,
             max_tokens=160, sample=None, ctx=256):
    """Generate with tool injection. Yields events; the caller decides what to do with them.

    step(ids)      -> logits for the next token, given the full id list (caller may window it)
    encode(text)   -> list[int]
    decode(ids)    -> str, specials INCLUDED (the protocol is made of them)
    run_tool(call) -> (result_str, elapsed_ms). Called only for a complete <tool>...</tool> span.
    sample(logits) -> int. None means greedy argmax, which is what the device does.

    Events:
      ("tok",  id, piece)                 one generated token
      ("tool", call_span)                 a call is complete; run_tool has NOT been called yet
      ("res",  result, ms, ok)            the tool returned
      ("inj",  ids, piece)                the result span was fed back into the context

    <res> is suppressed before every choice. The runtime supplies results; a model that can emit
    its own <res> can fabricate one, which is the whole failure this protocol exists to prevent.
    """
    ids = list(prompt)
    out = []
    for _ in range(max_tokens):
        lg = step(ids[-ctx:])
        lg[res_id] = -1e30
        n = int(lg.argmax()) if sample is None else int(sample(lg))
        ids.append(n); out.append(n)
        piece = decode([n])
        yield ("tok", n, piece)
        if n == end_id:
            return
        if n == toolc_id:
            m = TOOL_RE.search(decode(out))
            call = m.group(0) if m else None
            yield ("tool", call)
            result, ms = run_tool(call) if call else ("!give", 0.0)
            yield ("res", result, ms, not result.startswith("!"))
            inj = encode(f"<res>{result}</res>")
            ids += inj; out += inj
            yield ("inj", inj, decode(inj))


def generate_text(step, encode, decode, prompt, **kw):
    """The whole generation as one string. What an eval harness almost always wants."""
    parts = []
    for ev in generate(step, encode, decode, prompt, **kw):
        if ev[0] == "tok": parts.append(ev[2])
        elif ev[0] == "inj": parts.append(ev[2])
    return "".join(parts)
