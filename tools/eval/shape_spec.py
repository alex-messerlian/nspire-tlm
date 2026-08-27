"""SPEC VALIDATION for the structural call check -- docs/ARCHITECTURE.md section 6.

WHAT THIS IS AND IS NOT. It is NOT the shipping implementation: that is C, in src/store/toolrun.c,
on the parser tools/eval/parser.c already provides, wired into all three graders per
docs/WIRING_AUDIT.md. This file exists so the RULE was decided against the known traces before it
was written into a document -- a spec validated by nine cases rather than asserted.

Run it: python3 tools/eval/shape_spec.py   (exit 0 = the rule still decides all nine as specified)
"""
import ast, re, sys
from fractions import Fraction

CANON = {"^":"**"}
def to_py(e):
    e = e.replace("^","**")
    e = re.sub(r"(?<![A-Za-z0-9_.])(\d)\s*([A-Za-z_])", r"\1*\2", e)   # implicit mul
    return e

def norm(node):
    """Canonical shape: operator name + SORTED children for commutative ops, ORDERED for the rest.
    Numbers become their value; names stay names."""
    if isinstance(node, ast.Expression): return norm(node.body)
    if isinstance(node, ast.Constant):   return ("num", float(node.value))
    if isinstance(node, ast.Name):       return ("var", node.id)
    if isinstance(node, ast.UnaryOp):    return ("neg", norm(node.operand))
    if isinstance(node, ast.Call):       return ("fn", node.func.id, tuple(norm(a) for a in node.args))
    if isinstance(node, ast.BinOp):
        op = type(node.op).__name__
        if op not in ("Mult", "Add"):
            # NOT associative and NOT commutative: order is meaning. d/t and t/d must differ.
            return (op, (norm(node.left), norm(node.right)))
        kids = []
        def flat(n):           # flatten associative chains: a*b*c is one node, not two
            if isinstance(n, ast.BinOp) and type(n.op).__name__ == op:
                flat(n.left); flat(n.right)
            else: kids.append(norm(n))
        flat(node)
        return (op, tuple(sorted(kids, key=repr)))
    raise ValueError(ast.dump(node))

def leaves(s):
    if s[0]=="num": return [s[1]]
    if s[0]=="var": return [s[1]]
    if s[0]=="neg": return leaves(s[1])
    if s[0]=="fn":  return [x for a in s[2] for x in leaves(a)]
    return [x for a in s[1] for x in leaves(a)]

def ops(s):
    if s[0] in ("num","var"): return []
    if s[0]=="neg": return ["neg"]+ops(s[1])
    if s[0]=="fn":  return [s[1]]+[x for a in s[2] for x in ops(a)]
    return [s[0]]+[x for a in s[1] for x in ops(a)]

def check(formula, bindings, call_arg):
    """formula: 'U=m*g*h'   bindings: {'m':'2.0','g':'9.81','h':'5.0'}   call_arg: '(2.0)*(5.0)'"""
    rhs = formula.split("=",1)[1]
    for v,val in bindings.items():
        rhs = re.sub(rf"(?<![A-Za-z0-9_]){re.escape(v)}(?![A-Za-z0-9_])", f"({val})", rhs)
    want = norm(ast.parse(to_py(rhs), mode="eval"))
    got  = norm(ast.parse(to_py(call_arg), mode="eval"))
    reasons = []
    if want != got:
        wo, go = sorted(ops(want)), sorted(ops(got))
        if wo != go: reasons.append(f"operator multiset {wo} != {go}")
        wl, gl = sorted(map(str,leaves(want))), sorted(map(str,leaves(got)))
        if wl != gl:
            missing = [x for x in wl if x not in gl]
            extra   = [x for x in gl if x not in wl]
            reasons.append(f"operands differ: missing {missing}, unsourced {extra}")
        if not reasons: reasons.append("same operators and operands, different structure/order")
    return (not reasons), reasons

CASES = [
 ("mgh, the observed failure",
  "U=m*g*h", {"m":"2.0","g":"9.81","h":"5.0"}, "(2.0)*(5.0)",                 False),
 ("mgh, the correct call",
  "U=m*g*h", {"m":"2.0","g":"9.81","h":"5.0"}, "(2.0)*(9.81)*(5.0)",          True),
 ("mgh, correct but reordered  (must PASS: * is commutative)",
  "U=m*g*h", {"m":"2.0","g":"9.81","h":"5.0"}, "(9.81)*(5.0)*(2.0)",          True),
 ("v=d/t, the device E2E failure (RESULT_DEVICE_E2E.md)",
  "v=d/t",   {"d":"150.0","t":"49.0"},         "(150.0)*(150.0)",             False),
 ("v=d/t, correct",
  "v=d/t",   {"d":"150.0","t":"49.0"},         "(150.0)/(49.0)",              True),
 ("v=d/t, INVERTED  (must FAIL: / is not commutative)",
  "v=d/t",   {"d":"150.0","t":"49.0"},         "(49.0)/(150.0)",              False),
 ("feedback.jsonl trace: fabricated operand",
  "v=d/t",   {"d":"240","t":"30"},             "(240.0)*(13.0)",              False),
 ("KE, coefficient dropped",
  "KE=0.5*m*v^2", {"m":"4.0","v":"3.0"},       "(4.0)*(3.0)^2",               False),
 ("KE, correct",
  "KE=0.5*m*v^2", {"m":"4.0","v":"3.0"},       "0.5*(4.0)*(3.0)^2",           True),
]
bad = 0
for name, f, b, call, expect in CASES:
    ok, why = check(f, b, call)
    mark = "OK " if ok == expect else "*** SPEC WRONG ***"
    if ok != expect: bad += 1
    print(f"  {mark} {name}")
    print(f"        {f}  call: {call}   -> {'PASS' if ok else 'REJECT'} (expected {'PASS' if expect else 'REJECT'})")
    for r in why: print(f"          reason: {r}")
print(f"\n{len(CASES)-bad}/{len(CASES)} cases decided as specified")
sys.exit(1 if bad else 0)
