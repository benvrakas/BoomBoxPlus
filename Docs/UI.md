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
- Opening a Boom Box that already has Custom Music loaded flags the page in `NativeConstruct`.
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

## Layout: built-in fallback vs. native styling

`UBBPMusicPage` and `UBBPTrackRow` hold all behaviour, and every visual element is a
`UPROPERTY(meta = (BindWidgetOptional))`. If the widget tree is empty when initialised (i.e. the plain C++
class is used), they build a **functional fallback layout** — plain UMG, placeholder colours in
`BBPWidgetStyle`. That lets the whole feature be tested in-game before any art work.

**Native FICSIT styling (the chosen UX) is the next editor task:**

1. Create a Widget Blueprint with parent class `BBPMusicPage`; lay it out with the game's own widgets
   (`BPW_TileableButton`, the tape list's row style, game fonts) using the **same names** as the bind
   properties: `SearchBox`, `ResultsList`, `QueueList`, `NowPlayingText`, `PositionText`, `LyricText`,
   `QueueHeaderText`, `LibraryStatusText`, `PlayPauseButton`, `NextButton`, `PreviousButton`,
   `ShuffleButton`, `RepeatButton`, `ClearQueueButton`, `TapesButton`, `OpenFolderButton`, `RescanButton`.
   Any can be omitted.
2. Same for a row Blueprint (parent `BBPTrackRow`): `TitleText`, `DetailText`, `AddFrontButton`,
   `AddEndButton`, `PlayButton`, `MoveUpButton`, `MoveDownButton`, `RemoveButton`; set it as the page
   Blueprint's `TrackRowClass`.
3. Point the widget hook's `NewWidgetClass` at the page Blueprint (soft class path) instead of the C++ class.

Button labels are set through `BBPWidgetStyle::SetButtonLabel`, which expects the button's content to be a
`UTextBlock`; a Blueprint with icon buttons simply won't have its labels changed.

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

- Native styling (above) — needs the editor.
- Driving the vanilla Player page with our track info.
- Full gamepad focus (Blueprint pass).
- Hiding the overlay in menus.
