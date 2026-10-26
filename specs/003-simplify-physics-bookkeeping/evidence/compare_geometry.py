"""Verify every reachable shape operation against the preserved baseline, with explicit interface normalization."""
from pathlib import Path
import re
import json
PROJECT=Path(__file__).resolve().parents[3]
old=(PROJECT/'bin/physics-bookkeeping-20261025/baseline-source/src/physicsgeometry.cpp').read_text()
new=(PROJECT/'src/physicsgeometry.cpp').read_text()
def body(source,name):
    match=re.search(r'\b(?:void|bool)\s+(?:(?:PhysicsManager|Collision)::)?'+name+r'\([^;{}]*\)\s*\{',source)
    assert match,name
    start=match.end()-1;depth=1;end=start+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]
def normalize(text):
    text=text.replace('gravityVectorNormalized','gravityDirection').replace('calculateContactPoint','calculateBoxContactPoints')
    text=text.replace('collision->generateFallbackContactPoint();','generateFallbackContactPoint(collision);')
    text=text.replace('circleCircleCollision(pair);','circleCircleCollision(pair, gravityDirection);')
    text=text.replace('circleBoxCollision(pair);','circleBoxCollision(pair, gravityDirection);')
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    return re.findall(r'"(?:\\.|[^"\\])*"|[A-Za-z_]\w*|\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[fFuUlL]*|\S',text)
report={}
for name in ('circleCircleCollision','circleBoxCollision','checkProjections','updateContactGeometry','calculateContactPoint'):
    before=body(old,name)
    if name=='calculateContactPoint':
        start=before.index('    ColliderType referenceType');end=before.index('    // the edge vector',start)
        removed=(PROJECT/'specs/003-simplify-physics-bookkeeping/evidence/unreachable-clipping-branches.txt').read_text()
        assert before[start:end]==removed
        before=before[:start]+before[end:]
    after=body(new,'calculateBoxContactPoints' if name=='calculateContactPoint' else name)
    a,b=normalize(before),normalize(after)
    assert a==b,(name,'Reachable geometry tokens changed')
    report[name]={'reachable_tokens':len(a),'equal':True}
fallback=body((PROJECT/'bin/physics-bookkeeping-20261025/baseline-source/src/collision.cpp').read_text(),'generateFallbackContactPoint')
fallback=fallback.replace('{\n','{\n    const auto *referenceObject = collision->getReferenceObject();\n    const auto *incidentObject = collision->getIncidentObject();\n',1).replace('    setContactPoints(fallbackClipPoints);','    collision->setContactPoints(fallbackClipPoints);')
assert normalize(fallback)==normalize(body(new,'generateFallbackContactPoint'))
report['fallback_midpoint']={'equal':True}
report['removed_unreachable_circle_branches']='Single clipping caller follows both returning exact-circle dispatches; no accessor/friend added.'
print(json.dumps(report,indent=2))
