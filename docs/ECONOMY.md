# Economy in Infinity / Xalaria 0.3

Press **F4** or **Trade** on the atlas. Choose a world, inspect its markets, then use **Travel and dock**. Remote prices are previews; buying, selling and station services require the ship to be physically docked. City exchanges require a landing on their world. Change Station / City cycles that world's available venues.

A new commander starts with **20,000 GC**, a **30-unit hold**, **80 fuel**, and Technology/Culture Level 1. Existing flight saves receive the new economy once. Your original Infinity saves remain separate.

## A first trading trip

1. Open F4 near your starting world and request docking. The flight view shows the approach and docking lane.
2. Buy a small load of metal. Select quantity with the minus/plus buttons or brackets.
3. Use **Find profitable route** to preview a market within three directed wormhole hops. Estimates include the spread, current fees and expected fuel cost; prices can change.
4. Use **Undock / launch**, then fly there using the atlas. Open its market and request docking, then sell the cargo.
5. Refill fuel before a long journey. Use Charters for sealed-parcel work with a posted destination, reward and deadline.

A wormhole leg uses 8 fuel; a local guided warp uses 1. Fuel is charged as the journey progresses. Pausing, resuming or restoring the same journey retains that progress. Manual thrust and manual warp also use fuel. A dry tank disables warp; reserve manoeuvring jets remain available for reaching a berth.

Cargo adds physical mass to Infinity's ship controller. Bulk units represent ten kilograms here. Sealed parcels and ornaments each occupy one hold unit. Traders can exchange for successively larger holds, through the 600-unit Beluga capacity.

## Mining and production

**Services** sells the Tremble tool (1,500 GC) and, at advanced stations, the Precision tool (6,000 GC). Nature stations sell the Mine Filling Tool (100 GC) instead. Land, open Mining and survey the world. Deposits follow its published mine count, hold 1–20 units, and become harder to work as they empty. A successful strike transfers actual units into the hold. Tools consume energy and recharge between strikes. Filling a worked deposit closes extraction and restores up to your Technology Level in worked levels per use.

**Holdings** lists completed assets, unfinished frames and lost holdings. **Services** seeds a station, seeds a private vault or clears a field at the selected world while the ship is there.

- Stations require Technology Level 4, advancement 1–22, seed nanomites and 60% energy. The construction matrix is rolled once at seeding. Feed the remaining nanomites and structural metal to complete the frame. Source NPC stations and your station frames share a three-station orbit limit.
- Vaults use seven nanomites and forty structural units. Keep one per orbit. Dock to store or retrieve private cargo. With a city supplier in the system, another three nanomites opens half-price fuel and a daily till.
- Farms require a solid, non-molten surface. Clear one unfinished field at a time, then feed 60–150 seed depending on its ground. Completed fields generate income; unguarded ones can burn.
- Frames expire after seven economic days without feeding. Materials arrive in your actual hold, so large projects require repeated supply trips.

Holdings buttons feed materials, collect tills and hire city guards. **H** builds a concert hall; **G** a gallery; **O** donates an authentic ornament; **T** changes a station's Trade flag; **U** adds vault fuel. Halls need five nanomites and forty appropriate stone units in this adaptation. Galleries open at three donated pieces; each new piece begins contributing on the following day. Cultural tills are protected from raids.

For a vault, **Left/Right** chooses a commodity, brackets choose quantity, **V** stores and **R** retrieves. Your selected commodity and quantity are displayed in Holdings.

## Goodwill, culture and time

Spending accumulates Trade goodwill, including fractions: one point per 4,000 GC. Each point pays 100 GC at the next Trade docking, once. More than fifty points unlocks the local Trade fee exemption and a Trade flag. A flagged station gives up half its base till for fleet protection.

City cultural visits cost credits and require sufficient Culture Level. Local music, art and dance are remembered individually; a favoured aesthetic is easier. Knowledge improves that city's prices and lets its guards serve a holding. The same discount affects both sides of a city's market to preserve the buying/selling spread.

Economic time runs at **60 times real time**, including time away: one economic day takes 24 real minutes. Holdings earn into their own tills, not directly into your wallet. Raids, farm fires, frame deadlines and charter deadlines continue while away. The ship stays at its saved position. A single atomic session checkpoint saves ship, cargo, credits, route, berth and economic time together. Invalid checkpoints are preserved and reported instead of being overwritten.

## Public rules and native adaptations

Primary reference: [Xalaria's public Traveller's Handbook](https://www.xalaria.com/handbook/), inspected 10 November 2026. Spatial facts come from the bundled public catalogue, snapshot 5923cc4f.

| From the handbook | Native implementation |
|---|---|
| 5% station ask / bid spread; rich-system prices; Trade spending and rebates | Preserved, with finite stock and quantity-sensitive quotes |
| Station nanomite and income anchors; 22 advancements; seeding matrix; structural metals; orbit limit | Preserved; intermediate anchor values use geometric interpolation |
| Vault materials, storage, city fuel supply; farms, seed, guards, fire and theft | Implemented in the local simulation |
| Finite deposits, depletion rolls, tool prices, energy and restoration | Implemented using the catalogue's mine counts and stable local deposit seeds |
| Trader hold capacities and prices, experience thresholds | Preserved; an exchanged craft receives half its old purchase price as local trade-in balance |
| Culture requirements, favoured aesthetic, price benefit, guarded income, galleries and protected cultural tills | Implemented with locally generated city identities and requirements |
| Parcels and delivery income | Native contracts use the source reward tiers, sealed cargo and a seven-day deadline |

The public export does **not** provide the live market ledger, all 29 commodity definitions, faction assignments, city identities, current player holdings or full server formulas. This build implements the **twelve verified bulk goods** below. Other prices, stock, the 2% nonmember fee, fuel rates, 60x clock, local culture requirements, event schedules, 5-nanomite/40-stone hall recipe, generated contract destinations and starting funds are explicit local balance choices. They are not presented as recovered server values.

| Bulk good | Native base GC per unit |
|---|---:|
| Aluminium | 40 |
| Iron | 50 |
| Tin | 65 |
| Nickel | 120 |
| Chromium | 180 |
| Titanium | 320 |
| Iridium | 2,800 |
| Marble | 220 |
| Granite | 160 |
| Construction nanomites | 2,000 |
| Seed | 28 |
| Quantum neurocells | 5,000 |

Nanomite and seed base prices are supported by the handbook; the other bulk bases are native balance values. The rich commodity and faction of each source station are deterministic local assignments. Multiple source communities on a world share one native Exchange. Gas-world extraction, Nature/Valour alliance ledgers, combat bounties, robots, population migration, sanctuaries, relic trading, multiplayer and the remaining server economy are outside this version.

Nothing is sent to Xalaria's server. These are independently implemented C++ rules for this offline hybrid; no Xalaria client code or handbook prose is included.
