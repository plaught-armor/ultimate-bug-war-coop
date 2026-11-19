# 483: client play-time collision changes on pickups and props (card137 WARNs)

Status: Open.

## Seen

c244 run c (b205, FED_C4M1): 135 card137 client play-time collision-change WARNs against 5 in c127-c4m1-r2. Classes: ammo pickups, benches, bins, zipline points; caller exe+0x3B3D1AE. The run covered far more of the map, so the count may be coverage, not a regression.

## Do

Name the function at exe+0x3B3D1AE and what calls it at play time on the client. Decide per class whether the client's collision now differs from the host's (sync gap: fix by class through the card137 path) or the change is the client's own local sim with the same end state (then the WARN is noise: narrow it).

## Done when

A C4M1 run where every card137 play-time change row ends with the client's collision equal to the host's, or the WARN is limited to real differences and shows 0.

## b206 findings (static and c244b205c logs)

Callers (logs/pdb_publics.txt): exe+0x3B3D1AE UStaticMeshComponent::SetCollisionProfileName (ZiplinePoint init);
0x4C618A1 AADPickupBase::OnUpdatePickupStatus; 0x4C5FE0C AADPickupBase ctor; 0x51D25D2
ASTCustomPickupBase::PersistentActorActivate; 0x5165410 ASTDamageableActorBase::OnSignificanceChanged (47 rows
"3 -> 0"); 0x51D1C61 ASTCustomPickupBase::OnSignificanceChanged; 0x37C2C74 UPrimitiveComponent::execSetCollisionEnabled
(Flakbug building BP).

- The 135 rows are 45 classes at the 3-line cap. 132 fall at +201..203 ms after the client's first Pong: the client's
  own map init after the card095 journal replay. The host ran every (class, caller) pair during its load (card098 host
  rows), except PersistentActorActivate (0 host rows).
- Pickups: the c137 pickup probe (load+10s and digest round) gives 51 rows per side, 0 differ; collider collision 1 on
  all 19 placed weapon pickups on both sides.
- Late rows: Bin_Large significance "3 -> 0" at +4 s (viewer-local LOD, which the host also runs for its own camera);
  Flakbug C_1 SkeletalMeshComponent 1 -> 0 -> 1 in the same ms at +13 s (net 0; the host's copy runs the same BP
  caller, card098 tally n=13).
- Not a sync gap. The WARN was narrowed in b206 (change_c137.cpp): play starts kC137InitMs (1000 ms) after the first
  Pong, and the significance callers get their own cause "own view" (no WARN). A generic BP execSetCollisionEnabled
  still WARNs, so the Flakbug round trip may still show 2 rows.

## Senior note (2026-11-17 22:49): stays open, narrowed

Map-init noise is gone (kC137InitMs, own-view cause). What is left is real: in c448b206a (C1M1) ammo, grenade and weapon pickups go collision 0->1 on the client by local sim (AADPickupBase::OnUpdatePickupStatus, exe+0x4C618A1) about 32 s after the Pong, and a host row reverts each 14.5 s later (Frag at (6453, 11426)). For those 14.5 s the client's pickup differs from the host's. Plus exe+0x37C2C74 rows: Bugnest at (-25155, -83605) 1->0, Flakbug at (-7103, -75235) 1->0->1, BP_Dropship_LandingBlocker_C 0->2. Do: find what OnUpdatePickupStatus reads on the client (likely per local player inventory or a respawn timer) and make the client keep the host's state (suppress the local write or apply the host row at once), by class for AADPickupBase. Then name exe+0x37C2C74 and check host end state for its three classes. Done when: a C1M1 run with 0 play-time card137 rows on pickups and every exe+0x37C2C74 row matched by a host row within 1 s.

## b207 (junior, 2026-11-17)

- What OnUpdatePickupStatus reads: AADPickupBase::OnUpdatePickupStatus (exe+0x4C61870) calls the virtual
  IsPickupEnabled (vt+0x760); ASTCustomPickupBase::IsPickupEnabled (exe+0x51CCF30) is bEnabled (+0x2A8) AND the
  persistent active flag (+0x408+0x18) AND IsSignificant (+0x428). Both extra terms are this machine's own view.
  ASTCustomPickupBase::OnSignificanceChanged (exe+0x51D1C40) calls OnUpdatePickupStatus, so each machine writes its
  PlayerCollider (+0x300) collision from its own significance (return address exe+0x4C618A1); the host's write is
  journaled as local inside g_c95_significance_depth, which is why the host "reverts" only on its own next update.
- Fix (input_diag.cpp c483_pickup_collision, both sides): a SetCollisionEnabled from exe+0x4C618A1 writes the
  collider from bEnabled alone (the host's state: card 145 E rows, card 221 set back); visuals keep the view's answer.
  Row "card483: <side> ... pickup <name> collider collision W (bEnabled), not the view's V".
- exe+0x37C2C74 is UPrimitiveComponent::execSetCollisionEnabled (the Blueprint call). In c448b206a both play rows
  have a host row within 1 s (LandingBlocker 0->2 client 610134618 / host 610134675; BlockingVolume 0->3 610250887 /
  610250893). The C4M1 rows (Bugnest, Flakbug) come from BP_OnSignificanceChanged inside the significance update;
  b207 change_cause classes every change inside g_c95_significance_depth as own view (change_c137.cpp).

## Closed (2026-11-18 01:23)

Fixed in b207 (3a4e79683266b40d): the client pickup collider follows bEnabled (+0x2A8) only; IsPickupEnabled also read persistent-active and IsSignificant, both viewer-local. exe+0x37C2C74 is UPrimitiveComponent::execSetCollisionEnabled (Blueprint call). c448b207a: 0 card137 pickup rows; the 2 left rows have a host row within 1 s (LandingBlocker 0->2 client 617170049 / host 617170086; BlockingVolume 0->3 client 617290830 / host 617290839). Client WARN total 174 (b205a) -> 4.

Not done: nothing in scope.

Reopen when: a card137 play-time row on an AADPickupBase on the client, or any card137 row with no host row within 1 s.
