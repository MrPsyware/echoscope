# EchoScope interface conventions — 0.10.0

- **Rotate = items.** Clockwise advances an aircraft, forecast period, log entry,
  station, airport or information-menu entry. It wraps at either end. Changing items
  starts their first page. The radar retains its range/aircraft/altitude rotation modes.
- **Outer segments = items.** One segment per available item, starting at twelve
  o'clock and proceeding clockwise; the selected item is bright. A single item has
  one continuous segment. The radar's outer ring remains its watch alert ring.
- **`<` / `>` = pages within the selected item.** Both wrap at the ends. Their touch
  areas are wider than the glyphs (100 pixels inward, y=125–335). No arrows for a
  one-page item. Aircraft details retain their eased slide and swipe support;
  information/logbook pages switch immediately. All item navigation is consistent.
- **Centre tap / short mechanical press = radar**, except Information's menu where
  tapping or pressing selects an entry. Tap the menu's bottom area to return.
- **Two-second stationary touch = Information**, from any ordinary awake view.
  Movement cancels the hold; the release tap is consumed. It does not trigger while
  the mechanical button is held or setup is visible. First interaction during sleep
  is still consumed to wake the screen.
- **Five-second mechanical hold = setup**, unchanged. Notifications dismiss on
  a tap/press; a long screen touch can open Information.

The radar footer is **range · aircraft count · ALT · filter**. The first three choose
what rotation controls; the active mode is bright and underlined. Tap the fourth
control to cycle **ALL → WCH → HEL → MIL → LGT → LRG**. It remains visible, replacing
the old top filter. Filters combine with range and altitude. WCH uses your configured
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
keep page arrows in the same place, and use the same short action to return to radar.
The separate touch hold prevents accidental menu entry while selecting aircraft.
