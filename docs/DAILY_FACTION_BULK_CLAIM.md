# Daily faction bulk-claim configuration

`ui.daily_bulk_claim_factions` defaults explicitly to `"federation,klingon,romulan"`.

| Config value | Main-faction dailies eligible for bulk claim |
| --- | --- |
| Missing, empty or whitespace-only | The default: all three factions |
| `"federation,romulan"` | Federation and Romulan |
| `"none"` | None of the three |

Other dailies remain eligible. Use `none` by itself. Settings reload when the TOML file changes.

On upgrade, existing blank configurations keep their all-factions behavior.
The loader now resolves blank to the explicit default before populating the
runtime faction list and generated configuration values. `none` is a new explicit
opt-out; it does not share the blank/default representation.
