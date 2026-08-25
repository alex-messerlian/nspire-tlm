#ifndef NS_TOOLRUN_H
#define NS_TOOLRUN_H
/* The runtime side of the tool contract: find a completed call in what the model has emitted, run
 * it, and build the result span to feed back in.
 *
 * This lives in its own file rather than inside device_app.c for one reason: device_app.c includes
 * libndls and only builds for ARMv5TE, so anything in it can only be verified by a device
 * round-trip. Here the SAME code that ships is compiled on the host and checked against evalcli,
 * which is what TOOL_SPEC 5.1's byte-identical requirement actually means. */

/* Extract the LAST complete <tool>...</tool> span from `doc`. Returns its length, or 0 if there is
 * no complete span. The model can emit more than one call in a document and the one to execute is
 * the one that just closed, not the first in the buffer. */
int tlm_extract_call(const char *doc, char *out, int cap);

/* Execute a span and write "<res>RESULT</res>". Always writes something: a failure yields the
 * evaluator's own refusal code, never an invented value and never a fallback to what the model was
 * about to say. Returns 1 if the evaluator returned a value, 0 if it refused. */
int tlm_result_span(const char *span, char *out, int cap);
#endif
