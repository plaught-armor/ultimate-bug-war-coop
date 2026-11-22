# 609: The card 310 journal keeps a raw tactical support class and applies it later
Status 2026-11-22: open, parked until after the user's playtest.

Open 2026-11-22 (junior, found by the card 608 sweep). Static find, not traced to a failure, not seen in a run.

`C310Journal::default_class` is a raw UClass pointer, kept until map end. It is written by `c310_sync_default_class`
and by the client's 'D' receive (`c50_resolve`). Two gameplay paths use it later: the values given to a new pawn when
there is no host pawn (`v.cls`), and the replay's `c50_class_path`.

The journal holds no reference. If no pawn property references the class (for example a 'D' row that arrived before
the client had a pawn), a collection could free it and those paths would use freed memory. The map arm's print is
fixed by card 608 and is not part of this card.

Next: trace whether the class can be unreferenced while journalled. If it can, keep its path or a weak key and
resolve it at use. Done when that trace or fix is in the card.
