# EchoScope interface conventions — 0.14.0

- **Rotate = items.** Clockwise advances an aircraft, forecast period, log entry,
  station, airport or information-menu entry. It wraps at either end. Changing items
  preserves the current page, clamping to the last available page if necessary.
  The radar has range/aircraft/altitude/type rotation modes.
- **Outer segments = items.** One segment per available item, starting at twelve
  o'clock and proceeding clockwise; the selected item is bright. A single item has
  one continuous segment. The radar's outer ring remains its watch alert ring.
- **`<` / `>` = pages within the selected item.** Both wrap at the ends. Their touch
  areas are wider than the glyphs (100 pixels inward, y=125–335). No arrows for a
  one-page item. Aircraft details retain their eased slide and swipe support;
  information/logbook pages switch immediately. All item navigation is consistent.
- **Single mechanical click = open selection**, on radar or Information's menu.
  In setup it returns to radar; inside detail pages it leaves the view unchanged.
- **Double mechanical click = radar**, or Information when already on radar and
  information features are available. Click recognition waits 400 ms, as on Mini.
- **Hold and turn = mode/page.** On radar cycle Range / Aircraft / Altitude / Type;
  inside an item cycle its available pages. Turning consumes release and cancels
  the five-second setup action. Clockwise advances in all modes, including range.
- **Centre tap = radar**, except Information's menu where tapping selects an entry.
  Tap the menu's bottom area to return. These are extra touch shortcuts.
- **Two-second stationary touch = Information**, from any ordinary awake view.
  Movement cancels the hold; the release tap is consumed. It does not trigger while
  the mechanical button is held or setup is visible. First interaction during sleep
  is still consumed to wake the screen.
- **Five-second mechanical hold = setup**, unchanged. Notifications dismiss on
  a tap/press; a long screen touch can open Information.

The radar footer is **range · aircraft count · Alt · Type**. Hold-and-turn
cycles all four modes. Tap an inactive control to select it; tap it again to
advance its value (range wraps, aircraft advances, Alt/Type cycle their filters).
Rotation adjusts the selected control. The active mode is bright and underlined.
Altitude and Type values appear together underneath. Type cycles
**ALL → WCH → HEL → MIL → LGT → LRG**. Filters combine with range and altitude. WCH uses your configured
watchlist; HEL is rotorcraft; MIL uses the feed's military flag; LGT is emitter
categories A1/A2 (light/small); LRG is A3/A4/A5 (large/high-vortex/heavy). Unknown
classes remain available under ALL and may match WCH/MIL. These are feed categories,
not precise aircraft-size estimates. Filtering happens before the 64-aircraft limit.

Weather's next hour, today and next-three-days are three forecast items. Family
flight has one item with tracking/route pages. Stations rotate through the configured
stations in their overhead view. Stargazing groups multiple visible passes for one
station as pages within that station's item, alongside the outlook item. The logbook
shows the latest eight encounters; full retained history is on the server web page.

Consistency recommendation: keep the outer ring informational rather than tappable,
keep page arrows in the same place, and use double-click or centre touch to return to radar.
The separate touch hold prevents accidental menu entry while selecting aircraft.

Information menu order: Logbook, Family flight, Nearby airports, Weather / clouds,
Stargazing tonight, Space stations. Unavailable features are omitted.

Display sleep continues polling every 30 seconds; fresh visible watch matches use
5-second polling. Optional watch wake returns to sleep after the match leaves,
unless the user interacts. Range, type and altitude filters apply. Failed requests
retain the last match for at most 60 seconds and keep normal error backoff.
Explicit remote screen-off suppresses watch wake until a successful no-match feed.

Airport markers are drawn behind aircraft and trails. Small outlined
squares with a symbolic runway bar and optional airport-code labels use a separate
configurable colour/brightness. Tap a drawn icon or label to open its airport details. Aircraft hit testing has priority; hidden markers/labels have no hit area. The server includes the selected airport even when it is outside the nearest nine. Crowded
labels and markers are suppressed against other airports, never against aircraft; use web setup's Info server section to filter
scheduled airline airports and airport sizes, or disable the overlay.

Nearby airports uses the overlay's airline/size filters before selecting nine
nearest entries. Rotate changes airports while keeping details/approach page.
Side arrows toggle airport details and a north-up 20 km radius radar. The item ring
always represents airports; centre tap/double-click returns to main radar. The approach
page labels its use of the current home feed and coverage limitations.

The highlighted live aircraft retains its orange symbol and label. Its trail is
thicker/brighter than other trails and uses recorded altitude at each segment's
newer endpoint: green <5,000 ft, cyan 5,000–14,999 ft, blue 15,000–29,999 ft,
purple ≥30,000 ft; unknown altitude is muted. Trail points and heights are retained
and thinned together. The saved spotting log's track display is unchanged.

Radar bottom controls hide after 30 seconds without control interaction. A mode
click or bottom touch reveals the controls and performs the normal action on the
same input. Rotation extends the timer while visible; hidden rotation keeps
operating the selected mode without revealing the bar. Waking reveals it too.

Approach radar uses the main radar's full 210-pixel radius, aircraft symbols,
altitude colours, trails and sweep. An optional airport-centred 20 km map is fetched
only for the displayed airport, reusing the main/logbook map buffer. Runways, title,
coverage text and navigation remain overlaid. Map attribution remains visible in
muted green on every map view, including when radar controls are hidden.
