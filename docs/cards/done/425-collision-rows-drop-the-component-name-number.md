# 425: collision rows drop the component name number

## Finding
5 `WARN card132: recv ... CollisionOp ... NODE_AddStaticMeshCompo 0 result skipped: no placed copy matches` at the
C5M1 join (`logs/c399b122a-client.log`).

Cause: `c95_relay_collision` (`mod/src/ubw/door_c95.cpp:1257`) sends `object_name(component).text`. `object_name`
returns the FName text without its number, so the construction-script components
`NODE_AddStaticMeshComponent-3_<n>` of the 5 `BP_PylonSpline_C` go out as `NODE_AddStaticMeshComponent-3`.
The client's `c95_component` (StaticFindObject) finds none: the card 421 pair fails, the client asks for the whole
row, and that fails too. No visible defect today only because the client's own load writes the same value.

## Fix
Send the component name with its number (`c50_name` or `prop_name(component + kObjectName, 8)`); the host journal key
becomes per component. Sibling sweep: every wire name built with `object_name` that the receiver resolves by name
(card 136 effect rows and the rest); list each with a verdict in the report.

## Proof
Chain run: 0 such WARN rows at the C5M1 join; `card421` "asked for" 0 on C5M1; CollisionOp bytes stated before and
after.
