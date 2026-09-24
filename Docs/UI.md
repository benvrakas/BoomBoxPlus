# UI

## Where it lives

The vanilla Boom Box window is `BPW_BoomBox` (`/Game/FactoryGame/Equipment/BoomBox/`), whose root has a
`UWidgetSwitcher` variable **`mWidgetSwitcher`** with two pages:

```
BPW_BoomBox            parent Widget_UseableBase_C -> UFGInteractWidget (C++)
  mWidgetSwitcher
    [0] BPW_BoomBox_Player       now-playing + transport
    [1] BPW_BoomBox_TapeSelect   ScrollBox of BPW_BoomBox_TapeButton
    [2] BBPMusicPage             <- ours, injected
```

`BPW_BoomBox` also has Blueprint variables `mBoomBox` (the Boom Box being viewed), `mCurrentTape`,
`mIsPlaying`. All of this was read out of the `.uasset` name tables, not the editor — confirm in the editor.

## Injection

`UBBPGameInstanceModule`'s constructor creates a `UWidgetBlueprintHookData` subobject (SML's widget hook
type is a `UDataAsset`, but works fine as a subobject) targeting `BPW_BoomBox_C`, parent `mWidgetSwitcher`
(`Direct` mode — it's a variable), adding `UBBPMusicPage`. SML registers module widget hooks automatically.
If the page never appears, check the log for SML's hook validation errors; if `mWidgetSwitcher` turns out not
to be a variable, switch `ParentWidgetType` to `Direct_Any`.

## Showing the page

- Choosing Custom Music in the tape list calls `AFGBoomBoxPlayer::BeginChangeTapeSequence`; an after-hook
  calls `UBBPMusicPage::NotifyTapeChanged`, which flags every open page for that Boom Box.
- Opening a Boom Box always lands on the **vanilla first page**, even with Custom Music loaded — the user
  asked for that explicitly; the page is reached through the Custom Music button. (An earlier build
  auto-opened the page and was reverted.)
- **Showing is driven by `FTSTicker`, not the page's `NativeTick`.** First in-game test: the page was
  flagged but never appeared, because Slate only ticks painted widgets and a `WidgetSwitcher` only paints its
  active child — a hidden page never ticks, so it can never switch itself visible. `RequestShow()` switches
  immediately, then re-asserts for 0.5 s on the core ticker in case the window picks its own page a frame
  later (logged as "switched away from the music page; switching back").
- Selecting a tape **closes the whole Boom Box window** — vanilla behaviour: the character plays the
  insert-tape animation, and `LoadTapeNow` fires from its anim notify ~1 s later. Reopen to see the page.
- The first (player) page gets a **Custom Music** button beside Change Tape (`UBBPOpenMusicButton`, hooked
  into `BPW_BoomBox_Player` with `Indirect_Child` on `mChangeTape`, i.e. inserted into whichever panel holds
  that button). The music page has **< Back** (player page) and **Tapes**. If the Boom Box doesn't have
  Custom Music loaded, the page says so and offers **Play through this Boom Box**, which calls
  `BeginChangeTapeSequence` with the Custom Music tape.
- `< Tapes` returns to whichever switcher page's class name contains `TapeSelect` (not a hard-coded index).
- The page finds its Boom Box by walking its outer chain for a `mBoomBox` object property.

## Layout and the Satisfactory look

`UBBPMusicPage` and `UBBPTrackRow` hold all behaviour, and every visual element is a
`UPROPERTY(meta = (BindWidgetOptional))`, so a Blueprint subclass can still replace the layout later. With
no Blueprint, they build their layout in C++ from the **game's own assets**, loaded at runtime by path
(`BBPWidgetStyle`):

| Element | Game asset |
|---|---|
| Buttons | `BPW_TileableButton` (the button on every vanilla machine window), wrapped by `UBBPGameButton` |
| Text | `/Game/FactoryGame/Interface/Font/DescriptionText` composite font, typefaces Regular / SemiBold / Bold |
| Search icon | `/Game/FactoryGame/Interface/UI/Assets/Shared/SearchIcon` |
| Seek slider handle | `.../ConstructorWindows/ManufacutringMenu_Overclock_SliderHandle` (the overclock slider) |
| Colours | FICSIT orange `#FA9549`, window `#18181A`, recessed panels `#0E0E10`, rows `#28282B` (stored as linear values) |

Every asset load falls back to a plain look and logs once if the asset is missing, so a game update that
moves an asset degrades the style instead of breaking the page.

`UBBPGameButton` finds the tileable button's inner `mButton` (`UButton`) and re-broadcasts its `OnClicked`,
and sets the label through the widget's `mText` property plus its `SetText` Blueprint function
(`ProcessEvent`, checked to take exactly one `FText`).

Page layout: title bar (Back, CUSTOM MUSIC, library status, Open Folder, Rescan, Tapes), an orange rule, a
now-playing panel (track, lyric line, seek slider + time, transport), then two columns — search and results
on the left, the queue on the right — and the link panel at the bottom.

## The Custom Music button on the vanilla page

`UBBPOpenMusicButton` is injected beside `mChangeTape` (Indirect_Child hook). It is built from the same
`BPW_TileableButton` class, and on construct copies `mButtonStyle`, `mOverrideWidth/Height`, `mIconSize`
and `mWrapTextAt` from the Change Tape button, plus its horizontal/vertical box slot padding, size and
alignment, so the two sit and look alike. The log line `UI: Custom Music button matched to Change Tape
(slot ..., parent ...)` records what it matched.

## Driving the vanilla Player page

The vanilla page only follows Wwise playback, which we cancel, so its progress bar never moved. The
playback controller now calls the page's `IFGBoomboxListenerInterface` events itself for every listener in
the Boom Box's `mStateListeners` (access-transformer accessor):

- `CurrentSongChanged` and `PlaybackStateChanged` (`MayActuallyPlay` / 0) when the track, channel or
  play state changes for that listener;
- `PlaybackPositionUpdate(position, duration)` every 0.25 s.

## Seeking

The mod page's seek slider sends `RequestSeekTo` when released (mouse or gamepad capture end); playback
updates don't move it while dragging. The vanilla progress bar stays display-only: it is a `ProgressBar`
inside a Blueprint, and making it draggable would mean overlaying a slider on it by widget hook — possible
later, but the mod page slider covers the need.

## Link panel

Shows this Boom Box's channel code and how many Boom Boxes share it, a 4-digit code field, Link and Unlink
(Unlink only while shared). See Multiplayer.md.

## Controls in v1

Queue reordering uses **Up/Down buttons** plus a remove `X` on every row; drag-and-drop can be added in the
Blueprint pass. Buttons are also what makes the queue usable on a gamepad. `FGFocusableWidget`
(`GetWidgetToFocus`) is **not implemented yet** — needed for proper gamepad focus, per the plan.

Search runs 0.25 s after typing stops, over the local library only. Spotify/YouTube links arrive with
`BoomBoxPlusNet`.

**Gamepad focus:** `IFGFocusableWidget::GetWidgetToFocus` is `BlueprintImplementableEvent`, so C++ can't
implement it — full support belongs to the Blueprint pass. As a stopgap, `ShowPage()` calls the window's
native `UFGInteractWidget::SetDefaultFocusWidget` with the play/pause button and focuses it.

## HUD overlay

`UBBPHudOverlay` is added to the viewport (Z-order -10, hit-test invisible) by the playback controller on
every non-dedicated machine. It shows:

- the current lyric line, bottom-centre, and
- a "NOW PLAYING" card, top-right, for 4 s when the track changes (fading out over the last 0.5 s),

**only while the local pawn is within hearing range** of a Boom Box playing Custom Music
(`UBBPPlaybackController::GetAudibleRange()`, the attenuation's inner radius + falloff = 250 m). Both are
toggled by the mod config. Same `BindWidgetOptional` pattern as the page: `LyricText`, `NowPlayingBox`,
`NowPlayingTitleText`, `NowPlayingArtistText`.

It stays visible in the pause menu for now.

## Not done yet

- Full gamepad focus (Blueprint pass).
- Hiding the overlay in menus.
- A draggable vanilla progress bar (see Seeking).
