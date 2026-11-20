# Infinity / Xalaria 0.68.0

A native C++20 exploration prototype combining **Xalaria's published universe and wormhole map** with **your existing Infinity flight engine and Hyperion/Metal renderer**.

Unzip the macOS build and open **Infinity Xalaria.app**. The macOS 26+ arm64 app runs offline with the engine built in. Published builds are available from the [GitHub releases page](https://github.com/maetyu-d/infinity_xal/releases).

The 82 occupied regions have four looks. **Charred Glass** covers 22 regions. **Verdant Fog**, **Tidal Blue**, and **Acid Etch** cover 20 each. The atlas shows the selected region's look, and the flight view blends to it as you enter. Oylanhai has the charred palette, Ziksa has green haze, and Phiat has dark, white-capped currents suspended among the stars. The currents stay in place as the ship rolls. Search for Eudaya to see Acid Etch, with hot pink skies, sulphur-yellow mineral crust, and pitted slate surfaces.

Warp uses layered ember trails and a soft fade around the destination.

The 0.68 update adds a **persistent living universe and player fleet**. All **37,441 published orbital stations**, across **7,714 star systems in 82 occupied regions**, maintain inventories, industry and garrisons. Scheduled convoys load from those same inventories. Refineries consume finite ore and nanomites; farms produce food; fabrication consumes metals; shipyards turn supplied metal into defence strength. Shortages change prices and weaken defence. Factions commit existing garrisons to expeditions, intercept supplies, fight near station windows and can take ownership of stations.

Press **F10** for Fleet Command:

- Board a surrendered or ion-disabled ship and press **C** to claim its surviving hull and remaining cargo. The original hull becomes your vessel; it does not spawn a second copy. Very small craft cannot be claimed.
- Press **H** aboard the prize or docked at its home port to commission a captain and repair its drives. Rescued captains can take the post. **H** also repairs commissioned vessels when you meet them at their port or aboard.
- Dock at an accessible station and use **B** for a 6,500 GC freighter or **W** for a 12,000 GC warship. Each hull also consumes 10 iron, 6 nickel and 2 titanium from that station. Captains and repairs cost extra.
- Select a destination system in the **F2 atlas**, return to F10, select a ship and issue **0 Hold**, **1 Follow**, **2 Trade**, **3 Escort**, **4 Blockade**, or **5 Salvage**. Orders use actual directed wormhole routes. Captains wait for an occupied boarding collar to clear.
- Trade captains buy finite profitable cargo using your treasury, carry it, sell against the destination's inventory and cash, and charge a fee on income. They attempt a return leg; unfunded or unprofitable orders hold. Contraband needs a Rebel or Independent market.
- Escorts accompany a scheduled service from their current station to its published destination. Surviving delivery earns a one-time payment; missed departures and disabled freighters can fail. Nearby fighting uses visible ships and normal weapon traces.
- A blockade commits your actual ships against a station's defenders and incoming supplies. Several warships may be needed. Sustained victory changes the station's faction and access, leaves an evacuation lifeboat, and lets you collect accumulated traffic fees while docked there with **T**. Seized supplies remain finite cargo aboard your fleet. Nearby blockaders fight incoming escorts and use ion fire to strand freighters; the convoy escorts can return fire against blockaders and opposing fleet ships. A disabled freighter keeps its remaining cargo aboard throughout a long boarding attempt.
- Destroyed ships leave persistent wrecks. Target one and close within 1.2 km in free flight, then use **S** to salvage iron or **R** to recover survivors. Both are finite. An autonomous salvage ship searches known wrecks in its destination frame and holds recovered goods until you send it to trade.

**Tab** switches Fleet, Earned Intelligence and Discovered Campaigns. A fresh game has no remote reports. Docking, local radio conversations, **N** scans, captured route charts and captains' arrival reports earn intelligence; **I** buys an adjacent report from a dockside informant. Browsing the atlas does not reveal it. **A** declares support for the local station's faction. Opposing expeditions can become hostile, and attacks, rescues and victories affect standing and station access.

![Fleet Command with a captured hull, commissioned captains and a completed escort](Previews/Fleet-Command.png)

The strategic turn is five minutes of **playing time**. Distant production and combat use bounded strategic calculations; nearby squadrons exchange physical weapon hits. Captains, cargo, orders, wrecks, survivors, intelligence, station ownership and industrial inputs are saved together. Earlier sessions migrate without fast-forwarding years of wars. This is a prototype simulation: shipyard garrisons are readiness points represented by a bounded local squadron, and it does not simulate every crew member or every off-screen shot.


The 0.67 explosion update makes destruction effects **20% larger**, with **50% more particles** across fire lobes, secondary flashes, debris, embers and each propellant’s vapour cloud. The visibility envelope grows with the blast; fuel colours, lifetime, damage, collision and camera-shake strength are retained.

The 0.66 exterior pass rebuilds the large-scale planetary materials. **Lava worlds have connected cooling plates and incandescent boundaries**, broad melt pools, red cooling margins and filtered fine fractures. Rocky orbital relief separates quieter plains from folded mountain provinces, with darker impact bowls and brighter ejecta. These are globe-fixed material features; the existing terrain and collision surfaces remain authoritative.

Gas giants gain smaller wake vortices trailing their major storms. Stars have deeper, optically thin coronal sheets and streamers around the limb, clipped by foreground depth. Photospheric granulation fades before it becomes pixel noise, while broader thermal and magnetic regions remain visible. The four regional environments, high-contrast film curve, vintage optics and manufactured cockpit finishes are preserved. The pass adds no render targets, texture downloads or temporal image history.

![Connected molten plate boundaries and darker cooling crust](Previews/Graphics-066.png)

![Filtered photosphere and layered coronal streamers around Oylanhai](Previews/Graphics-066-Star.png)

![Storm bands and satellite vortices on the ringed gas giant](Previews/Graphics-066-Giant.png)

![The renewed orbital materials in green space](Previews/Graphics-066-Verdant.png)

The 0.65 graphics pass gives the cockpit a **sculpted copper pressure manifold, machined couplers and telescopic canopy stays**. Rounded alloy, polymer sleeves and recessed mounts catch distinct highlights. Sunlight now follows the actual star’s colour, apparent size and eclipse visibility. The canopy fittings cast soft-edged shadows across the manufactured cabin, and restrained shafts of forward-scattered light show through the windscreen. The window shadow boundary stays smooth during rapid turns, while fully lit highlights and covered shadows keep their contrast.

Dry, low-pressure worlds gain globe-local impact basins with smoothed bowls, terraced raised rims, central peaks and filtered ejecta rays. Their sunward rims shade the bowl, and heat and pressure fade their preservation. This is orbital material relief, with the existing terrain and regional coatings retained. The deep blacks, sharp central image and Summar/Elmar-inspired glow remain unchanged. The pass reuses the cabin buffers, with no extra render targets or old-frame image history.

![Rounded canopy fittings, machined collars and stellar lighting](Previews/Graphics-065.png)

![The renewed cockpit in green space](Previews/Graphics-065-Verdant.png)

![Actual starward approach with sunlight entering the cabin](Previews/Graphics-065-Sunlit.png)

![The ship interior with manufactured finishes and crisp outlined crew](Previews/Graphics-065-Deck.png)

The 0.64 renderer adds **live cockpit and station reflections**, tracing the visible scene with each surface’s actual normal and finish. Polished alloy and coated composite pick up nearby fittings and light through window openings; matte paint and fabric stay subdued. A new surface attachment shares the cabin’s four-sample edge coverage. Roughness filtering and confidence fades keep reflections bounded, and there is no old-frame history. Off-screen geometry uses the existing analytic lighting. `--no-reflections` selects the original two-attachment cabin path.

Planets gain globe-local relief intersections, shaded gullies and layered rock detail. Lava worlds have broader dark cooling rafts around molten lowlands. A fixed, filtered background starfield adds distant points behind the four regional environments, with no random twinkle or preferred up direction. Flight readouts now use the same sharp atlas type as the ship’s displays, fitted to their existing bounds after popup occlusion. The restored contrast and vintage lens character are preserved.

![Live cabin reflections, darker lava crust and sharp flight readouts](Previews/Graphics-064.png)

![Orbital relief, cloud banks and a fixed celestial starfield in Ziksa](Previews/Graphics-064-Verdant.png)

![A procedural freighter against the renewed sky](Previews/Graphics-064-Ship.png)

![The ship interior with outlined crew, physical lighting and reflective finishes](Previews/Graphics-064-Deck.png)

![The station apron through the glass docking bridge](Previews/Graphics-064-Station.png)

The 0.63 finish pass gives manufactured surfaces richer light: brushed alloys have directional highlights, painted hulls catch a separate clear-coat reflection, and copper, carbon composite and worn coatings keep distinct finishes. The long cockpit roof strips cast integrated light and continuous, roughness-filtered reflections. Ship and station fixtures have broad, stable highlights. Sparse service covers, recessed seals and latches sit in hull coatings without extra overlapping surfaces. Atmospheric worlds have denser billowing cloud banks, sunward extinction and projected cloud shadows; incandescent worlds keep darker basalt beneath the molten glow. The restored contrast and Summar/Elmar-inspired optics are unchanged. Verification includes all six gameplay suites, four environment types, station traversal, a freighter at three render scales, and a 960-frame full-boost recording.

![The final material and lighting pass in the cockpit](Previews/Graphics-063.png)

![Sunlit cloud relief and manufactured cockpit finishes in Ziksa](Previews/Graphics-063-Verdant.png)

![Filtered hull service covers, painted panels and exposed alloys](Previews/Graphics-063-Ship.png)

![Station materials and the live apron through the docking bridge](Previews/Graphics-063-Station.png)

The 0.62 camera pass adds a **Summar/Elmar-inspired vintage photographic look**: a crisp centre, progressively softer edges, warm highlight halation and preserved scene contrast. The 0.62.1 adjustment removes the lifted blacks, reduced gain, desaturation and extra highlight compression, keeping deep shadows and bright highlights. Off-axis bright points open into elongated cat’s-eye bokeh that follows the image circle. The same optical character covers the cockpit, ship and station views; the HUD is composited afterward and stays readable. Existing `--no-lens-effects` also disables the cabin treatment. The lens uses fixed sampling with no animated grain or temporal image history.

![Vintage glow with a sharp centre and softer outer cockpit](Previews/Graphics-0621.png)

![The vintage camera treatment in green space](Previews/Graphics-0621-Verdant.png)

The 0.61 graphics pass rebuilds the cockpit's visible fittings with a segmented carbon-and-alloy pressure collar, recessed amber lighting, copper pressure lines, fitted overhead cassettes, machined rotary controls and deep turbine vents. Warm collar light and cool roof light pick out their physical bevels, with matching reflections on polished fittings. Depth-based contact shading grounds consoles, seats, machinery and station interiors using the existing cabin depth buffer. Four-sample edge coverage smooths thin fittings before they are composited; contact shading uses no shadow history or animated noise. Freighters have exposed pressure tanks, retaining hoops, coolant harnesses and split thermal armour; patrol and survey engines also gain service machinery. Bare metal and opaque paint have distinct highlights, and freighter tanks and thermal armour remain present at all three detail levels.

![Layered canopy armour, recessed lighting and machined cockpit controls](Previews/Graphics-061.png)

![The redesigned cockpit under green environmental light](Previews/Graphics-061-Verdant.png)

![An actual procedural freighter with exposed tanks, coolant pipes and thermal armour](Previews/Graphics-061-Ship.png)

The 0.60 graphics pass adds finer rock relief seen from orbit, darker lava crust with hotter branching fissures, and gas-giant storms with feeder bands, deeper eyes and cloud-height shading. Temperate atmospheric worlds have eroded cloud banks, fine cirrus and sunward shadows; all planets now receive their actual star direction for this shading. Dust lanes and green fog have finer filaments and stronger internal depth. The four regional palettes remain distinct. The cockpit and hull stability fixes are preserved and checked with another 960-frame acceleration recording.

![Dark basalt plates and incandescent fissures above Oylanhai](Previews/Graphics-060.png)

![Sunlit atmospheric cloud banks and planetary relief](Previews/Graphics-060-Verdant.png)

![Storm bands on a ringed gas giant in Ziksa](Previews/Graphics-060-Giant.png)

The 0.59 graphics pass adds clear-coated carbon-composite consoles, cleaner brushed finishes and broader painted hull panels with filtered identification stencils. Thrust feedback uses a slower, restrained vibration over the existing pressure lean. Material normals and relief are calculated in a scale-safe frame; bevels retain one hull-local texture projection, and fine gloss detail fades before it can shimmer. Lens depth thresholds follow the current render scale. The native verification includes a 16-second, 60 fps sequence of full boost, lateral thrust, braking, reverse thrust and roll, plus a nearby freighter viewed at three render scales.

![Carbon-composite consoles and polished cockpit fittings](Previews/Graphics-059.png)

![Painted hull panels, stencils and engine bells on the procedural freighter](Previews/Graphics-059-Verdant.png)

The 0.58 graphics pass gives freighters, ferries, patrol craft and survey ships distinct painted hulls and exposed alloy finishes, with open engine bells, recessed glowing throats, radiator fins and service hardware. Docking aprons have denser panel work, inset louvred lamps and maintenance grilles. Contact shading adds depth where nearby structure meets. Stellar convection has darker lanes and fine bright filaments; carbon giants have deeper veils, and planet rings have filtered particle clumps.

![A procedural freighter with open engine bells, radiators and hull panelling](Previews/Graphics-058.png)

![Apron plating and contact shading through Phehmyar’s docking bridge](Previews/Graphics-058-Station.png)

The 0.57 graphics pass adds machined windscreen fittings and instrument fasteners, warm and cool lighting across the cockpit, directional brushed-metal reflections, ceramic clear coats, and distinct painted hull panels. Temperate planets with atmospheres have a globe-anchored optical cloud deck with sunlit relief and shadows. Charred space has darker internal glass laminations and finer gold edges, with deeper dust lanes behind it. Fine material details filter out below pixel size.

![Machined cockpit fittings, split lighting and fractured lava crust](Previews/Graphics-057.png)

![Atmospheric cloud relief and reflected regional light in Ziksa](Previews/Graphics-057-Verdant.png)

## Convoys, ambushes and faction battles

**F9** opens Operations. Convoy services now cover **all 7,714 systems across all 82 occupied regions**. They depart every thirty minutes of flight time along published, directed wormhole links. Systems without their own station act as transit stops between connected ports. Routes repeat as scheduled services; crews, pirate ambushes and faction interdictions are seeded for each departure.

Find operations by docking at a station, hailing an actual convoy, or hearing a nearby convoy broadcast. A fresh Operations log is empty. Browsing the atlas and opening the list do not reveal the rest of the universe's departures.

Select a discovered operation and press **E** to accept escort duty, **P** to mark it as a pirate prize, **F** to follow with helm assist, or **I** to hail it. Follow clears your current berth, holds a safe position beside the freighter, crosses the route's wormholes and rejoins the formation at the exit. Manual thrust, turning or **B** returns control to you. Ordinary station navigation and docking also release convoy assist.

Convoys contain a freighter, two escorts, two pirates or faction interdictors, and a distress-response patrol. These are physical ships with shared rendering, shields, weapon traces, collision paths and boarding collars. Escorts defend their freight; pirates and opposing factions fire at NPC ships; patrols respond to threats. Crews withhold shots when a friendly ship blocks their line of fire. A ship you attack can turn its guns on you. Nearby fights use live flight and weapon simulation. Away from the player's system, service outcomes resolve with a lightweight seeded model that carries recorded losses forward; the game does not run thousands of simultaneous 3D battles.

Freight is allocated from local producers as a separate wholesale lot. Each departure has a **finite manifest**, independent of shop inventory. Surviving deliveries replenish destination markets; a lost or stolen shipment creates a temporary supply shortage. Cargo, damaged ships, casualties, discovered reports, duties, reputation and payments persist in your save. The flight clock pauses when the game is closed.

For an escort payment, accompany the freighter for **at least 45 seconds during its outbound or inbound passage**, preserve **at least 60% of its manifest**, then physically dock at its destination and press **R** in Operations. Payment is recorded once and raises the sponsoring faction's standing. Shooting the convoy voids its escort credentials. Standing also contributes to earned faction contacts in Dispatch.

For piracy, disable the carrier with ion fire, demand surrender in **F8**, secure its collar, then walk through the airlock to its transfer bay. **E** transfers its remaining manifest into your hold, subject to your capacity. A robbed ship does not replenish itself after reload, its delivery remains interrupted, and the pirate receives warrants and a faction penalty. Stolen goods still need an Independent or Rebel fence.

The **Eunhia Station → Phehmyar Station** service carries **12 nickel**. Its escort payment is **2600 GC** for a complete delivery. The end-to-end tests cover both defending that shipment through the wormhole and intercepting, docking with and robbing the same service in a separate journey.

![Discovered convoy and its finite manifest](Previews/Convoy-Operations.png)

![Escorting the Eunhia nickel shipment through an ambush](Previews/Convoy-Battle.png)

![Boarding the captured convoy for its nickel](Previews/Convoy-Prize.png)

## Pirates and warships

Your role comes from what you do. Trading, rescue, scouting, exploration and investigations remain available with every fit. Dock at a station and press **F8** to buy a **Corsair** (3500 GC) or **Gunship** (9000 GC). The Corsair has twin pulse guns, an ion disruptor, four torpedoes and reinforced armour; the Gunship has three heavier pulse guns, ten torpedoes, heavier armour and an 80-point shield. The Civilian fit retains a light defensive pulse and its original hold.

In manual flight, **M** arms or safes weapons, **hold Space** fires, **T** cycles visible ship targets and **1 / 2 / 3** select pulse, ion or torpedoes while the radio is closed. Weapons generate heat, need a clear line of fire, and are inhibited while walking, docking, warping or cruising. Pulse guns damage shields then hull; ion fire disables engines while preserving a boardable hull. Torpedoes have finite ammunition and travel to their target.

Destroyed ships erupt in large reactor blasts: white-hot flashes, expanding fire clouds, spherical shock fronts, secondary detonations and tumbling incandescent fragments. Blast size follows the ship's size. Debris spreads in all directions and cools over ten seconds, while a dark wreck remains. Player and NPC kills trigger the same effect once; reloading an old wreck does not replay it. Ion-disabled ships remain intact for boarding. The blast and fragments are visual effects and add no area damage.

Explosion colour follows a persistent, procedurally assigned propellant family, independent of the star system's palette. **NTO/hydrazine** produces orange flame and a translucent rust-brown release cloud. **RP-1/LOX** produces yellow-orange fire and dark soot. **LH2/LOX** produces blue flame with violet fringes and very little residue; **methane/LOX** has a deeper blue-violet burn and light residue. Gas clouds absorb light instead of glowing like fire, expand in all directions, and do not rise toward an artificial up direction. Metal fragments can still glow amber within a blue fuel fire. These are visual ship-design profiles; station fuel trading is unchanged.

The colours are stylized for gameplay visibility. The brown oxidizer cloud is inspired by [NOAA's description of the nitrogen tetroxide/nitrogen dioxide equilibrium](https://cameochemicals.noaa.gov/report?key=CH4075). Clean hydrogen flame is deliberately made more visible than its [nearly invisible real appearance](https://spinoff.nasa.gov/node/10136). This is not a combustion chemistry or pressure simulation.

![An escort's reactor blast and expanding shock front](Previews/Huge-Explosion.png)

![A methane-fuelled raider's blue-violet blast](Previews/Blue-Explosion.png)

For piracy, close within **2.5 km**, select a freighter and use **Demand cargo** in F8. Captains can refuse, flee and report you. Damaged or disabled ships can surrender and lower their shields. Request the collar approach in F8, dock manually or use **J** for automatic docking, then **F5** and **E** at the airlock doors to board. **E** in the transfer bay seizes its remaining cargo. Each ship has a finite prize; distant demands cannot transfer cargo into your hold. Stolen goods sell at **Independent or Rebel markets**.

Attacking civilians, threatening their cargo and taking it creates a saved local warrant. Lawful stations refuse clearance and trade at 500 GC of outstanding fines; security ships intercept nearby fugitives. Independent and Rebel stations offer refuge. Settle a system's warrant through F8 while near one of its worlds; security control accepts payment even when station clearance is refused. Refitting does not erase your record.

For patrol work, hunt procedural **Rook raiders**. Disabling or destroying one earns a single **1200–2000 GC patrol voucher**, redeemable at lawful stations through F8. Security and raider vessels use the same visible hulls, shields, collision paths, approach collars and boarding decks as other traffic. Their patrols are generated at orbital harbours throughout all 82 regions; encounters become active locally as you approach.

**Repair / reload** in F8 costs 16 GC per hull point and 250 GC per torpedo. If your hull reaches zero, F8 offers emergency recovery at an outlaw haven: cargo and active freight contracts are lost, recovery costs up to 5000 GC, and warrants remain. Existing saves load as Civilian ships and keep their journeys, cargo, discoveries and investigations. Combat records, stolen stock, surrendered vessels and live torpedoes persist in new saves.

![Corsair armament and ship fits](Previews/Corsair-Armament.png)

![Boarding a captured freighter for its prize cargo](Previews/Prize-Boarding.png)

## Dispatch

Stories begin as discoveries. A fresh journey starts with an empty Dispatch log. Press **N** at the helm to scan your current world or stellar space, use **K** then **I** to select and hail nearby crews, or cross a station's airlock and spend a few seconds inside to meet a local contact. After listening in a system for twenty seconds, an active local emergency can arrive as a public broadcast. Map browsing never grants stories.

The universe still contains **960 authored incident outlines**, covering distress, disasters, war and conflict, factions, beliefs and cults, and science. These premises and choices are assigned procedurally to worlds, ships and stations. The player sees only leads they have uncovered. Follow-up chapters combine the response with shared investigation and hearing structures.

Press **F7** to open Dispatch. **Nearby** shows discovered, active leads within three wormhole jumps, five at a time and with at most two urgent. **Local leads** shows what you have learned in the selected system. **Discoveries** replaces the complete archive and records only the cases you have found, including their source. Scanning the same world or repeatedly hailing the same crew does not generate extra tips.

Completing a case chapter or delivering sealed freight earns trust with its faction. Established contacts offer private leads; completing a full three-chapter case earns access to their most confidential work. Returning to a contact after earning trust can reveal a new lead. Unfinished branches stay hidden until their preceding chapter is completed.

Use **T** to track a found lead and **Tab** to move between Nearby, News, Local leads, Follow-ups, Journal and Discoveries. **Enter** plots its location or opens the local approach or radio action. From the required orbit, or after crossing the correct airlock, choose a response with **1** or **2**. Each chapter pays once.

World events continue on the flight clock even when undiscovered. Faction crews respond, incidents become contained or escalate, and responses succeed or fail. **News** carries updates about incidents you already know, with a two-minute delay per wormhole hop. Relief and conflict continue to affect convoy timing, local traffic advisories and supply prices. These are scheduled procedural responses, rather than fleet combat simulation.

Discoveries, scan history and contact introductions survive reloads. Older saves preserve their tracked cases, filed reports and earned follow-ups; they do not reveal the full story library during migration.

Investigations work throughout the universe, in all six story channels. New leads start with an **approximate search area**. Track one with **T** and use **Enter** to approach its area. Press **N** within 6 km to narrow the bearing, then examine the actual object within **180 m** at less than **25 m/s**. Recover its paired recorder within **75 m** at less than **8 m/s**. The final check requires boarding the relevant witness's ship or station and pressing **N**, or scanning a separate control probe within **140 m** at less than **15 m/s** in survey cases.

Press **B** inside Dispatch to see the case file: signal bearing, physical trace, recovered recorder, and independent account. Recorders can indicate equipment failures, deliberate interference, or fabricated accounts. Witnesses and independent instruments can contradict the initial claim. Uncollected evidence stays unknown. At the ship's aft cargo terminal, **B** opens case files in flight or at a berth; **E** retains the docked cargo market.

Once the evidence is verified, send it to its sponsor or release it for independent review. Public findings about interference or fabrication can cost sponsor trust while earning independent standing and a larger payment. Local supply prices, convoy schedules and access advisories react for fifteen minutes. New follow-up chapters remain earned through completing their parent case. Cases already active in older saves retain their existing progression.

In Ziksa, the opening distress call comes from a freighter operating out of Phehmyar. Look for its double-flash beacon and uneven wake, hail it for clearance, then dock and cross to its transfer deck. Open **F7** there to help isolate the fault or arrange a tow.

After you arrange a tow, the follow-up takes you to **Eunhia Station** in Oylanhai. The physical Ziksa–Oylanhai wormhole exits near the station. Compare the flight recorder with the tow log and decide who should receive the evidence. If you help repair the drive circuit, the next assignment is an orbital survey for the load that triggered the failure.

[![A fresh journey starts with an empty discovery log](Previews/Dispatch-Nearby.png)](Previews/Dispatch-Nearby.png)

[![A case file with recovered physical evidence and a contradictory independent account](Previews/Dispatch-Discovery.png)](Previews/Dispatch-Discovery.png)

[![Approaching a physical experiment recorder for close recovery](Previews/Investigation-Recorder.png)](Previews/Investigation-Recorder.png)

## Graphics upgrade

- Planetary twilight follows the curved limb, clouds show deeper shadows and brighter sunlit edges, and mineral layers become visible on exposed slopes. Rings and cloud bands share the published equator. Very broad rings fade into a diffuse dust belt.
- Resolved stars have brighter convection, fine coronal streamers, and layered prominence loops. The Oylanhai–Ziksa wormhole bends the live background around a turbulent plasma rim.
- Charred Glass has overlapping refracting sheets. Verdant Fog has more defined folds and darker pockets. Tidal Blue has broken crests in world-anchored currents. Acid Etch has stronger hot-pink and sulphur-yellow deposits.
- Orbital stations have segmented glazed modules, bevelled armour and braced docking terraces. Freighters have more detailed hulls, cargo racks and engine housings. Settlement facades have deeper panel seams and glazing.
- The cabin has inset seams and deeper shadows at bulkhead joints. Light from the cargo windows picks up the colour of the surrounding region. The atlas uses stronger contrast around controls and a clearer star glow on the system chart.

[![Eunhia Station, Frak's rings, the Oylanhai wormhole and stars in three regional environments](Previews/Graphics-Overview.png)](Previews/Graphics-Overview.png)

## Cockpit

Hold **Shift** and move the mouse to look through the side windows, up at the overhead panel, down at the controls, or back toward the hatch. Release **Shift** to face forward. The heads-up display fades as you turn away from the windscreen. The windows sit behind continuous gaskets, and reflections soften near the edges of the glass.

Press **F5** from the pilot's seat to stand up. Walk with **W A S D** and look with the mouse or arrow keys. Hold **Shift** to walk faster. The rear hatch opens as you approach. Follow the passage to the crew quarters, systems alcove, or cargo bay. Return to the cockpit and press **E** beside the seat to take the helm. **Escape** releases the mouse while walking, and clicking the game captures it again.

Flight continues while you walk, including during the brief camera move between the seat and the deck. Release and press movement keys again when changing controls. Saved flights open at the helm. The destination name stays on the instruments, and the windscreen direction cue returns once the view steadies.

Three crewmates appear as animated 2D silhouettes. Amber, cyan, and mint outlines identify them as they walk between the navigation panel, crew quarters, systems alcove, and cargo bay. Their rounds take the ship's plotted course, hull condition, and cargo into account. They yield in the passage and wait for you to clear a blocked doorway.

Amber reading lights pick out the bunk and lockers. Cyan light from the cooling rack marks the systems alcove, and amber loading lights frame the cargo bay. Arrow plaques show which way to turn for each room. In the cargo bay, the sign ahead points to the terminal and the airlock.

Brushed alloy, ceramic panels, woven upholstery, and grip plates respond differently to the cabin lamps. Light strips cast soft halos around their housings, and shadows deepen around bulkhead joints. The cargo bay has glazed side windows onto the live exterior.

The navigation display faces into the passage and shows the current system and nearby wormhole, or the destination and progress of a plotted course. The systems monitor is mounted at walking height and gives hull and fuel separate gauges. The cargo display shows loaded units, free space, and the largest commodity load.

Once the clamps are secure, press **F5** and follow the passage to the cargo bay. Face the terminal on the aft wall and press **E** when the prompt appears. It opens the market at the station you're docked with and selects the largest commodity load and its quantity. Press **S** to sell or **B** to buy. Use **5** to open delivery charters and **Esc** to return to walking. The manifest stays visible in flight.

At Phehmyar Station 1, the daily charter takes sealed freight back to Eunhia Station 1 through the Ziksa–Oylanhai wormhole.

### Station boarding

1. Dock at any orbital station. Click a visible station in flight to open its market, or select its world and use **Change Station / City** in **F4**. Press **F5** after the clamps lock, follow the passage into the cargo bay, then look left near the aft terminal to find the **Airlock**.
2. Face the ship door and press **E**. Walk into the airlock, face the bridge door and press **E** again. The two doors cycle in sequence.
3. Walk across the docking bridge and follow the rooms to **Station Loading Bay**. Face the sealed container and press **E** to transfer the local charter freight into your hold. At Phehmyar Station 1 its destination is Eunhia Station 1.
4. Return through the bridge and press **E** facing the ship door from inside the airlock. Once aboard, turn back and press **E** to seal the door, or return to the helm to seal it automatically. **Enter** releases the clamps once the airlock is sealed.

Physical pickup and the onboard charter screen use the same daily charter, cargo limits, and delivery record. The dispatch board shows when the freight has already been collected or when the station has no work available. Saving while on the station preserves the berth and the freight, and reloads at the helm.

Room names settle and fade as you pass through a doorway. Station displays and available freight are visible before you enter their rooms. Distant screen text fades as it becomes too small to read. Airlock doors ease into and out of their housings, letting light spill farther across the deck as the opening grows. The threshold lights turn green when the doorway is clear to walk through.

### Modular stations

The station kit generates freight terminals, habitats, research stations, industrial stations, and private vaults. Docking collars and pressure doors use a common interface. Bridge segments lead to concourses, habitation galleries, laboratories, workshops, or vault antechambers and the loading bay. Exteriors combine structural spines, pressure pods, cargo stacks, habitat rings, solar wings, radiators, and antenna masts. All 37,441 catalogue stations and completed player-built stations and vaults use this system.

Slender metal arches support the curved glass canopy on each docking bridge. Looking inboard reveals the maintenance catwalk, cable runs, and inspection lights below the windows. Looking outboard gives a clear view of orbit beyond the station’s support arms. On the way back, you can see the exterior through the bridge windows from inside the station doorway.

The bridge looks over the station's service apron on one side and an open recess beside the runway on the other. The station hull stays visible from inside as you turn to look along the gantry.

Watch from the bridge or observation lounge as shuttles and cargo tugs approach the lit auxiliary berths, turn around, and head back out. Larger freighters pass farther from the windows. The traffic leaves the player docking approach clear.

Outside the station, freighters travel with escorts and shuttles run their own orbital routes. Research stations also have survey ships. Broken satellites, spent boosters and solar panels drift above the traffic. At the Oylanhai–Ziksa wormhole, service craft circle outside the opening and four pulsing buoys mark the approach.

Click a crewed ship or its contact row to hail it. **K** and **Shift-K** cycle crews in radio range, including from inside the ship, and **I** hails the selected crew. The radio panel gives you the flight plan, local advice, a rendezvous request and the crew's home-port market. Replies depend on the craft's assignment, position and current activity. If an orbital crew clears you to rendezvous, the helm matches its course at a moving holding point below the lane. **B** or manual flight controls return the helm to you.

During a rendezvous, traffic comms shows distance to the holding point and relative speed. Course match is acknowledged after both have settled. Press **5** to ask a crew about freight. They can quote the current station charter or point you to the delivery port for a parcel already aboard. **6** opens the relevant charter page, where **D** requests travel and **M** requests manual docking once in orbit. Collect the parcel at the pickup port and deliver it at the receiving port.

Press **7** in traffic comms to request docking with a crewed ship. Freighters, patrols, ferries, survey ships and gate service vessels can receive you if their hull can fit a transfer collar. The smallest shuttles and tugs stay on the radio. Follow the moving approach lights to dock manually or press **J** to let the helm complete it. Once clamped, stand up with **F5**, cycle the aft airlock with **E**, and cross the glass bridge to the other ship's transfer bay. Collect a sealed courier parcel from the receiving crew and deliver it to the port shown on the dispatch display. Return to your ship and seal the airlock before undocking.

The hulls and collars of traffic ships are solid. A strike pushes your ship clear, and the hull takes damage according to the impact speed. An impact during an approach cancels the docking attempt. The other ship corrects after a light hit. After a harder strike its crew calls on the radio and suspends docking during a brief collar inspection. Once the crew reports clear, request a fresh approach.

Your ship, patrols, heavy freighters, survey ships and gate service vessels carry shields. Small local craft and ordinary ferries fly without them. A raised shield absorbs ship impacts and recharges after a few seconds clear of contact. Look for a faint blue edge, a ripple at the point of impact, and the charge reading in the HUD or traffic radio. Docking clearance lowers both ships’ fields.

Moving ships leave twin wakes along the paths their engines travelled. The streaks curve with their manoeuvres and fade over about seventeen seconds, so you can still see a ship’s course after it has passed. Your own ship leaves the same kind of wake in normal flight. Wakes use cyan in Charred Glass, magenta in Verdant Fog, amber in Tidal Blue, and mint in Acid Etch.

Station interiors use finishes and fixture colours chosen for the local environment. Charred Glass stations pair oxblood upholstery and bronze with amber lamps. Verdant Fog uses forest enamel and moss textiles, Tidal Blue uses navy alloy and sea-green ceramics with rust-coloured seats, and Acid Etch uses plum and rose with yellow lighting. Coloured light from the glass bridges and observation windows falls across stitched fabric, glazed ceramics, brushed metal and worn tread plates.

Upholstery has a diagonal weave, rugs use a herringbone pattern, and acoustic wall panels have recessed perforations. Mineral-composite worktops show embedded stone chips. Brushed alloy, glazed ceramics, enamel seams, and raised floor treads have individual surface relief and gloss.

Reading lamps cast warm pools over the lounge tables, while cool task lights illuminate the analysis benches. Light from the observation windows carries the shadows of the window frames onto the floor. Ceiling strips reflect from polished surfaces, and furniture casts soft contact shadows where it meets the deck.

Starlight follows the local star's bearing and fades when a world eclipses it. The curved bridge canopy and lounge windows admit light through their openings. Nearby wormholes add coloured spill, and polished surfaces reflect the shape of the glazing.

Docking gantries have braced approaches and inset lane markers. Research stations carry dish arrays, habitats have glazed ring sections, and freight, industrial, and vault stations have distinct service structures. Inside, amber arrows lead to the loading bay and teal arrows mark the route back to the ship. Signs visible from both directions along the main corridor point into side rooms.

Signed doorways off the main corridor open into observation lounges at habitats, observation lounges and analysis labs at research stations, maintenance bays at industrial stations, dispatch offices at freight terminals, and archives in private vaults. Observation lounges have upholstered bench seating, rugs, planters, and an optics counter beside wide windows onto the live orbital scene. The analysis lab has a dedicated sample station and a display for the local system's orbital survey. Look for the **Main Corridor** sign to return to the route to the airlock or loading bay.

The generator uses the station's world and berth ID. The same module dimensions place the walls, openings, fixtures, signs, and freight pickup. New station layouts need no changes to the save format.

- The yoke follows the ship's pitch, yaw, and roll. The port throttle lever slides as you change thrust with **W** and **S**.
- The forward display shows velocity, bank, and either altitude or stopping distance. The port console shows throttle and fuel. The starboard scope shows the selected target's bearing and range alongside hull integrity.
- The overhead panel shows landing gear, flight assist, and docking status.

[![The bank readout and canopy as the ship rolls across Tidal Blue currents](Previews/Cockpit.png)](Previews/Cockpit.png)
[![Two-dimensional crew silhouettes aboard the ship](Previews/Ship-Interior.png)](Previews/Ship-Interior.png)
[![Navigation, systems, and cargo displays viewed from the deck](Previews/Deck-Displays.png)](Previews/Deck-Displays.png)

## Planets and stars by environment

Select any screenshot to open it at full size.

| Environment | Planet | Local star |
| --- | --- | --- |
| **Charred Glass**<br>Oylanhai | [![Eunhia with bright terrain contours against a charred sky](Previews/Charred-Planet.png)](Previews/Charred-Planet.png) | [![Oylanhai’s stellar disc framed by glowing glass bands](Previews/Charred-Star.png)](Previews/Charred-Star.png) |
| **Verdant Fog**<br>Ziksa | [![Sunlit Phehmyar in green fog](Previews/Verdant-Planet.png)](Previews/Verdant-Planet.png) · [![Cloud bands and debris rings on Frak](Previews/Frak-Rings.png)](Previews/Frak-Rings.png) | [![Ziksa’s stellar disc seen through green haze](Previews/Verdant-Star.png)](Previews/Verdant-Star.png) |
| **Tidal Blue**<br>Phiat | [![Mroyrn among dark blue currents](Previews/Tidal-Orbit.png)](Previews/Tidal-Orbit.png) | [![Phiat’s stellar disc beside suspended ocean swells](Previews/Tidal-Star.png)](Previews/Tidal-Star.png) |
| **Acid Etch**<br>Eudaya | [![Dark Yugve against hot pink and sulphur-yellow skies](Previews/Acid-Orbit.png)](Previews/Acid-Orbit.png) | [![Eudaya’s pink stellar disc in Acid Etch](Previews/Acid-Star.png)](Previews/Acid-Star.png) |

## Photographic lens in 0.5

Bright stars, lamps and glass glints now spread into an eight-blade aperture bokeh, with compressed cat-eye shapes near the frame edges. A depth-aware focus pass keeps the central scene in focus and gently softens foreground and distant detail. Warm halation, subtle barrel distortion, edge dispersion and a visible-star flare chain add a photographic lens character. The charred reds, deep browns and burning yellow-white highlights remain.

The effect uses the live HDR image and depth. It adds no random light orbs. The physical cockpit, target markers and interface are drawn afterward and stay sharp. `--no-lens-effects` disables the new camera treatment for comparison.

## High-contrast glass space in 0.4.1

Deep umber and crimson now sit farther below the incandescent yellow-white highlights. Space itself carries exaggerated curved glass sheets, dark reflective bodies, sharp bevel light and nested caustics. The sky is anchored to celestial directions; a depth-masked optical pass refracts the live background and adds a weaker second-surface image. Planets, stations and HUD geometry keep their positions.

This is an artistic optical material for the void. It adds no stars, world objects or collision boundaries. `--no-glass-space` disables the space effect for comparison. `--space-test --fresh --smoke-test` places a separate capture profile in open space.

## Glass and mirrors

Stations and settlements use painted alloy, glazed panels, concrete and their own fixture colours. Regional light falls on these finishes and appears in polished reflections. Pane interiors, panel joints and wear follow the structure. Planet surfaces, skies and haze carry the stronger regional palettes.

Reflections use current-frame scene colour, depth and material normals. They fade near the screen edges and fall back to a directional environment when the reflected object is outside the view. This is screen-space reflection with an environment fallback; it is not ray tracing. Reflection strength varies by surface and viewing angle, with roughness filtering to reduce distant sparkle. HUD text remains outside the reflection pass.

### Stations and settlements

| **Eunhia Station** · Charred Glass | **Phehmyar Station** · Verdant Fog |
| --- | --- |
| [![Eunhia Station and its docking deck against orange glass bands](Previews/Glass-Station.png)](Previews/Glass-Station.png) | [![Phehmyar Station’s ring and docking deck in green fog](Previews/Verdant-Station.png)](Previews/Verdant-Station.png) |
| **Mroyrn Station** · Tidal Blue | **Yugve Station** · Acid Etch |
| [![Mroyrn Station beside suspended blue currents](Previews/Tidal-Station.png)](Previews/Tidal-Station.png) | [![Yugve Station’s grey panels against the pink and yellow sky](Previews/Acid-Station.png)](Previews/Acid-Station.png) |
| **Phehmyar** · Surface city | **Iakay** · Surface city |
| [![An aerial view of the towers and streets on Phehmyar](Previews/Phehmyar-City.png)](Previews/Phehmyar-City.png) | [![An aerial view of Iakay’s buildings and streets under pink light](Previews/Iakay-City.png)](Previews/Iakay-City.png) |

## Firelight colour scheme

The two supplied fire photographs define the palette: near-black silhouettes, blackened wine and crimson shadows, saturated flame orange, amber light and isolated yellow-white heat. The materials, atmosphere, warp, physical cockpit, map and economy screens share it. Hot emission retains its brighter range, while ordinary surfaces keep dark edges and texture contrast.

The reference swatches are recorded in [data/firelight-palette.json](data/firelight-palette.json). The photographs were used for colour direction; they are not included in the package.

## Economy in 0.3

Press **F4** or **Trade** on the atlas to open markets, holdings, mining, services and delivery charters.

- Buy and sell twelve verified bulk goods at physical stations and city exchanges. Carry cargo to other stars, compare profitable routes, refuel and exchange for larger holds.
- Mine finite surface deposits with fitted tools, then sell or build with their output.
- Seed and feed stations, private storage vaults and farms. Collect their tills, hire city guards, add vault fuel and build cultural halls.
- Earn Trade goodwill and rebates; learn local aesthetics and donate authentic ornaments to galleries.
- Fuel, cargo mass, docking and wormhole navigation share the real ship. Remote markets are read-only previews.
- Economic time and holdings continue while away. Ship, route, wallet, cargo and economy save in one atomic checkpoint.

The rules use Xalaria's public handbook, with documented local balance where the live server data is unavailable. This is an offline economy, not a connection to the live game. See [the economy guide and source distinctions](docs/ECONOMY.md).

## Map integration

- The Galaxy view colours the starfield by regional environment. The **Links** button shows the full wormhole network. An amber dashed line marks a plan and a solid blue line marks the active course.
- The System chart uses the selected system's palette for its star, planet markers and orbit guides. The chart shows the selected planet's moons and the scrollable list includes every planet and moon in the system.

[![Galaxy map showing the ship and paused course to Ziksa](Previews/Atlas.png)](Previews/Atlas.png)
[![Ziksa system map with the selected and arrival worlds marked](Previews/System.png)](Previews/System.png)
[![Galaxy overview coloured by regional environment](Previews/Galaxy-Regions.png)](Previews/Galaxy-Regions.png)

### Fly the Oylanhai–Ziksa wormhole

- At Eunhia Station 1, press **F4** to trade and **D** on the station screen to release the clamps. Wait for the departure lane to hand you the helm.
- Press **H** in flight to target the wormhole. In the atlas, select **Ziksa** and choose **Target Wormhole** for the same bearing.
- Use the arrows or press **C** to steer with the mouse and line up with the ring. Increase thrust with **W**, reduce it with **S**, and fly through the centre. A crossing uses eight fuel.
- The exit puts you on approach to **Phehmyar Station 1**. Open **F4** and choose **Manual Dock** (**M**) or **Auto Dock** (**D**). Use **H** in Ziksa to find the return mouth.

[![The targeted wormhole on departure from Eunhia Station](Previews/Manual-Wormhole.png)](Previews/Manual-Wormhole.png)

### Dock by hand

In local orbit, select the station in **F4**, then press **M**. Fly to the outer pair of approach lights, face the berth and follow the lights inward. The docking director shows heading and roll error, the sideways offset and closing speed. Hold **R** to match the berth's roll.

Use **W/S** to move forward and back, **A/D** to strafe and **Q/E** to move down and up. Release the keys to match the station's velocity. Hold **Ctrl** for finer control near the amber clamp lights. When **Hold steady** appears, stay lined up for 1.6 seconds to engage the clamps. Open **F4** to trade, or press **Enter** to undock. Press **J** during a manual approach to switch to automatic docking, or **B** to brake and abort.

- Switch between the galaxy and an orbital schematic with **F3** or the Galaxy/System buttons. Select planets and moons directly in the diagram or world list.
- The live ship marker comes from the same physical position as the flight engine. The cockpit has a clickable navigation card with a route minimap, destination, leg number, range and progress.
- The planned selection appears in rust; the committed course appears in amber. Browsing another star or world leaves the committed course intact. A selected world also supplies the cockpit target name and bearing.
- Use **Pause/Resume** or **Space** to retain a course while braking. **Cancel** or **B** returns to manual flight. Manual steering pauses a guided course.
- Journeys save with the ship, including the remaining hops, precise final world and pause state. Existing saves from 0.1 retain their current flight target.
- Drag the galaxy view to pan. Scrolling zooms around the cursor. **Locate**, **Fit Route** and **Overview** make it easy to recover the ship or see the whole plan.
- Search results are visible and ranked with exact names first. World searches select the matching world; Tab and Shift-Tab cycle results. A failed search cannot launch a stale destination.
- Double-click a star to inspect its system, or a world to fly to it.

## What is playable

- Search the actual public star and world names, select systems on a native map, inspect planets and moons, zoom and pan.
- Plot the shortest available route through the published **directed wormhole links**. Disconnected stars remain disconnected.
- Press Enter to fly each route leg with Infinity's existing guided warp, collision checks, acceleration, cockpit and destination reveal. The ship moves continuously; route selection does not teleport it.
- Select a world in the current system and press Enter to approach it. Use manual thrust, mouse steering, landing gear and automatic landing.
- Save and resume the ship and navigation state. Saves are separate from the original Infinity game.

The downloaded explorer snapshot **5923cc4f** contains 7,710 stars, 48,081 planets and moons, 11,254 life-bearing worlds, and 6,041 communities. The four local survey systems bring the playable catalogue to **7,714 stars and 48,085 planets and moons**.

### Stellar survey

Search the atlas for **Rhyvane** (red giant), **Nereth** (neutron star), **Veyru** (brown dwarf), or **Cindrel** (carbon giant). The atlas also accepts the star type. Press **Enter** to fly to the system's survey world, then press **F6** for a magnified view of the star. **F6** or **Esc** returns to flight. The view uses enhanced exposure for the brown dwarf and a stylized depiction of the neutron star's jets.

## Controls

| Context | Control | Action |
|---|---|---|
| Anywhere | F4 | Economy / trade |
| Economy | M / D | Manual docking in local orbit / automatic docking |
| Manual docking | J / B | Switch to automatic docking / brake and abort |
| Docked | Enter | Release clamps and depart |
| Anywhere | F2 | Map / flight |
| Anywhere | F3 | Galaxy / system view |
| Active journey | Space | Pause / resume |
| Atlas | Type a name, star type or ID | Search stars and worlds |
| Atlas | Tab / Shift-Tab | Next / previous search result |
| Atlas | Click | Select star or world |
| Atlas | Enter | Fly selected route or world |
| Atlas | Drag / arrows | Pan the galaxy map |
| Atlas | Scroll | Zoom at cursor; scroll worlds when over the list |
| Atlas | Locate / Fit Route / Overview | Frame ship / journey / galaxy |
| Atlas | Home | Centre on the live ship |
| Atlas | Page Up / Page Down | Scroll the world list |
| Flight | W A S D / Q E | Forward, sideways and vertical thrust |
| Flight | Arrows | Pitch and yaw |
| Flight | U / O or V / Z | Roll left / right |
| Flight | Hold R | Level the wings in manual flight. Uses the local horizon near a planet and the system's orbital plane in open space. |
| Flight | C | Capture / release mouse steering |
| Flight | Hold Shift + move mouse | Look around the cockpit. Release Shift to face forward. |
| Flight | F5 | Stand up from the pilot's seat |
| Survey system | F6 | Open or close the magnified stellar view |
| Anywhere | F9 | Discovered convoys, escort duties, pirate prizes and reports |
| Operations | E / P / F / I / R | Accept escort / mark prize / follow / hail / report at destination |
| Anywhere | F8 | Ship fit, piracy, patrol vouchers, repair and rearm |
| Manual flight | M / hold Space | Arm or safe weapons / fire |
| Manual flight, radio closed | T / 1, 2, 3 | Cycle visible ship target / pulse, ion, torpedo |
| Captured ship transfer bay | E | Seize its finite prize cargo |
| Anywhere | F7 | Open discovered Dispatch leads; map browsing reveals no new stories |
| Flight | N | Scan the current world or stellar space for a lead |
| Ship interior | W A S D / mouse or arrows | Walk / look around |
| Ship interior | Shift / E beside the seat | Walk faster / sit at the helm |
| Cargo bay | E facing the aft terminal | Open the docked station's market and charters |
| Docked airlock and loading bay | E facing the door or freight | Cycle the pressure doors / collect sealed freight |
| Flight | Left Option (Alt) / Ctrl | Boost / precision |
| Flight | B | Brake and cancel the queued route |
| Flight | X / scroll | Cruise / drive setting |
| Flight | Y / J | Next planet / approach selected planet |
| Flight | G / L | Landing gear / automatic landing |
| Flight | Ctrl-Tab | Contact catalogue |
| Flight | F1 | Controls |
| Flight | Escape | Release mouse, then exit |

Interstellar travel takes time: warp speed increases with clearance from nearby worlds and stars. Opening the atlas leaves the simulation running.

## What was recovered

The public explorer exposes a star index and 82 region files. The index supplies source IDs, names, spectral classes, technology and directed wormhole links. Region records supply stellar radius and temperature, primary and moon hierarchy, radii, orbital distances and periods, temperatures, solid/gas classification, life flags, community/station counts, cloud descriptors and ring bounds.

The native importer reads the compact factual snapshot in [data/catalog.tsv](data/catalog.tsv). Source distances and radii are converted to metres; periods to seconds. The public explorer's scale is retained: one logical map step represents **10^15 metres**.

The map uses Xalaria's logical 200 by 200 grid with a gentle vertical lift. Infinity provides the detailed surfaces, lighting, atmospheres, cockpit and warp imagery. The game infers stellar mass from the published radius and temperature. Worlds retain their published temperatures, solid/gas classification and ring bounds. The game uses published orientations for 14,860 ringed worlds and the orbital plane for the 27 older records whose ring orientation is unavailable.

This is an **offline flight and economy prototype**. It does not connect to Xalaria's game server or import its multiplayer state, accounts, text adventure, robot AI or live player holdings. The local economy and construction rules are described above; the full server economy is not reproduced. Community and station counts are inspectable facts; procedural settlements are Infinity scenery rather than imported city layouts. Source orbital periods are retained; the scene starts from a fixed reference epoch and is not synchronised to the live browser game's clock. Infinity's microscopic material model uses a representative material on worlds hotter than its supported solid phases.

## Build

The delivered macOS 26+ arm64 application is statically linked to the existing Infinity engine libraries. For rebuilding locally:

~~~sh
cmake -S . -B build -G Ninja \
  -DINFINITY_ENGINE_ROOT="/path/to/Infinite"
cmake --build build -j 3
cmake --build build --target test
./build/infinity_xalaria
~~~

Set INFINITY_ENGINE_BUILD if the engine's build directory differs. The CMake file currently targets macOS using your existing compiled libraries. Windows and Linux packages have not been built.

The new entry point is [src/XalariaMain.cpp](src/XalariaMain.cpp). Catalogue loading and graph routing are in [src/Xalaria.cpp](src/Xalaria.cpp). The economy ledger is [src/XalariaEconomy.cpp](src/XalariaEconomy.cpp), with its guide in [docs/ECONOMY.md](docs/ECONOMY.md). The shared journey controller is [src/XalariaNavigation.cpp](src/XalariaNavigation.cpp); projections and picking are in [src/XalariaMap.cpp](src/XalariaMap.cpp). The other changes adapt Universe, Geometry, Flight, Planetology, Cockpit and Renderer. The project embeds its own checked-in shaders and verifies their source/binary hashes when configuring; it does not reuse the original Infinity shader header. The copied Infinity source is included for inspection; CMake compiles the replacement modules and links the remaining existing engine library. No JavaScript or browser runs inside the game.

Infinity baseline: the existing local working tree, version 0.69.0, with HEAD ee5f9de7f16a748cc254db44490ea13db3349495 and its pre-existing uncommitted changes. Engine archive SHA-256: 817b9dae58128fec6a6b4ccb5f88152641e754a674e4f2f8f285b4e194c06a56. Rebuild against this matching engine baseline.

Verification checks catalogue totals, every star address, representative world descriptors, moon parentage, ring bounds, map text bounds, disconnected and directed routes, real ship movement, save restoration, galaxy samples and a complete interstellar arrival. The integration suite also verifies a two-hop journey, destination stability while browsing, pause/resume, route checkpoints, frame-invariant ship coordinates, map picking and cursor-centred zoom. The economy suite verifies physically authorised transactions, conservation, cargo limits, a wormhole cargo haul, fuel progress, sealed delivery, finite mining, construction, vaults, farm guards, cultural income, offline settlement, expiry, corrupt-save rejection and UI bounds. Native captures are in Previews.

## Sources and attribution

- [Xalaria universe explorer](https://www.xalaria.com/universe/)
- [Public star index](https://www.xalaria.com/universe/data/index.json)
- [Public data-loader schema](https://www.xalaria.com/universe/js/universe-data.js?v=7)
- [Public explorer scale and presentation](https://www.xalaria.com/universe/js/universe-app.js?v=82)
- [Public economy and construction handbook](https://www.xalaria.com/handbook/)
- [Xalaria homepage](https://www.xalaria.com/)

Xalaria and its original names and universe data are by **Julian Cochran**. Its public availability is not an open-source licence. No Xalaria JavaScript, narrative prose, music, artwork or server code is included in this source package. This is a local adaptation for the requested project, not an official Xalaria client.

The source snapshot's retrieval details and SHA-256 hashes are in [data/provenance.json](data/provenance.json). Infinity's existing dependency notices are in THIRD_PARTY.md and third_party_licenses.


Shader changes can be rebuilt with the matching engine's shader compiler:

~~~sh
python3 tools/compile_shaders.py \
  --shaderc "/path/to/Infinite/build/shader-tool/shaderc" \
  --bgfx-include "/path/to/Infinite/external/bgfx/src"
cmake --build build -j 3
~~~


To check scene reflections, capture the same saved berth and frame with and without `--no-reflections`, then run:

~~~sh
python3 tools/check_reflections.py --on reflection-on/flight.ppm --off reflection-off/flight.ppm
~~~

The comparison verifies a visible change on the floor while keeping the HUD unchanged. The additional normal/material buffer uses eight bytes per scene pixel; the reflected colour is traced against the existing current-frame depth buffer.


For the glass-space and contrast comparison, capture matching open-space views with and without `--no-glass-space`. `tools/check_space_glass.py` checks that the background changes while the HUD remains unchanged; it can also compare the dark and yellow ranges in matching before/after berth captures.


Lens verification uses matching native captures with and without `--no-lens-effects`. The optical composite remains HDR through bright-light extraction and a 48-tap aperture gather, then receives one final display transform. Autofocus samples the central depth cross; geometric defocus is capped at five pixels, while highlights use a wider iris. It is an artistic screen-space camera model. Transparent surfaces share their opaque depth, and there is no temporal focus history. The additional optical and reduced-resolution targets use about 9.8 bytes per scene pixel.

~~~sh
python3 tools/check_lens.py --on lens-on/flight.ppm --off lens-off/flight.ppm
~~~
