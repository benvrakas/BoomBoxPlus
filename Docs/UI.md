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

`UBBPGameButton` hides the arrow icon the tileable button draws by default (clears `mIcon`, collapses the
`mIconObject` widget on construct), finds its inner `mButton` (`UButton`) and re-broadcasts its `OnClicked`,
and sets the label through the widget's `mText` property plus its `SetText` Blueprint function
(`ProcessEvent`, checked to take exactly one `FText`).

Page layout: title bar (Back, CUSTOM MUSIC, library status, Open Folder, Rescan, Tapes), an orange rule, a
now-playing panel (track, lyric line, seek slider + time, transport), then two columns — search and results
on the left, the queue on the right — and the link panel at the bottom.

## List performance (the 400-song crash)

The first version rebuilt the whole queue list — a new row widget per song, each with six game-button
widgets — on every queue change. Adding a Spotify playlist changes the queue once per song, so the page
created hundreds of thousands of widget objects, froze ~0.3 s per song, and crashed when the engine's
object array filled (`UObjectArray.cpp` line 648 fatal error, from `UBBPMusicPage::RefreshQueue`).

Now:

- `RefreshQueue` / `RefreshResults` only mark the list dirty; `NativeTick` rebuilds at most every 0.3 s.
  Hidden switcher pages don't tick, so a page nobody is looking at never rebuilds.
- Rows are pooled (`SyncRows`): existing rows are refilled, new ones are only created when the pool is too
  small, and extras are collapsed. Status lines and "Add all" sit outside the scroll boxes, so the lists
  contain rows only.
- At most 100 rows per list. The queue shows a window starting two songs before the current one
  ("Showing songs 41-140 of 408").
- The page has a fixed size of 90% x 86% of the screen (a `USizeBox` root, updated each tick from the
  viewport size and DPI scale). Before, it sized itself to its content, so the window jumped around as
  results and queue rows loaded.
- Row titles, details and the now-playing title are clipped with an ellipsis (`BBPWidgetStyle::Truncate`)
  so long YouTube titles no longer run under the row buttons.
- Row buttons are `UBBPCompactButton`: a flat `UButton` in the game's colours instead of the heavy
  `BPW_TileableButton`. Page-level buttons still use the game widget.

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
- `PlaybackPositionUpdate(position, duration)` **every frame**: the channel position and track length
  (`UBBPPlaybackController::GetVanillaPosition`, matching the Custom Music seek bar), or 0 / 0 for live streams.

**Progress bar.** The Boom Box also reports a position itself, from its own tick, which asks Wwise and so always
gets 0 for Custom Music. That tick only runs while the vanilla `PlaybackEnabled` flag is set, which the Turbo
Bass fix (Multiplayer.md) does locally, so from 1.2.4 the bar sat at 0:00 and flashed our value every 0.25 s.
Hooking the private `GetCurrentPlaybackPosition` (via a `Friend=` access transformer) was tried: the hook
installed but never fired, since the shipping build inlines it into the tick. What works instead is being the
last writer each frame: `ABBPPlaylistSubsystem` ticks in `TG_PostUpdateWork`, after every actor tick and timer,
and the controller sends the position every frame from there. The page is drawn after the world tick, so it
always shows our value.

**Text lines.** `BPW_BoomBox_Player`'s name table shows three: `mSongName`, `mArtistName` (from the `FSongData`
given to `SetCurrentSong`) and `mAlbumName`, which shows the tape's `mDescription` and is only re-read when the
page is told the tape changed. All three come from `UBBPPlaybackController::DescribeForVanillaPage`:

| | Song line | Artist line | Album line (`UBBPBlueprintLibrary::GetSourceName`) |
|---|---|---|---|
| YouTube / SoundCloud | title | artist (from an "Artist - Title" video title, else the uploader) | "YouTube: <channel>" / "SoundCloud: <account>" (`FBBPTrack::Uploader`) |
| Local file | title | artist tag, or "Unknown artist" | "Your music folder" |
| Radio, song announced | announced title | announced artist | "Radio: <station>" (or "YouTube live: <channel>") |
| Radio, nothing announced yet | station name | host (or YouTube channel) | same |
| Nothing queued | "Custom Music" | "BoomBoxPlus" | the idle blurb |

The Custom Music page uses the same source name, as "Source: YouTube: <channel>" under its title line. The
"Kind: name" form leaves room for new sources without a new layout.

Song and artist reach the page through a `GetCurrentSong` hook (the tape's `mPlaylist` is empty). `UpdateVanillaPages`
re-sends `CurrentSongChanged` when either line's text changes (a new radio announcement, not only a new queue
entry), and `CurrentTapeChanged` when the album line changes. Before 1.2.6 the album line was never refreshed, so
after the radio it kept showing station data during YouTube songs.

`mDescription` is a class default shared by every Custom Music Boom Box on this machine, so it's set right before
each page is told to read it (never from a `GetCurrentTape` hook, which `ABBPPlaylistSubsystem::RefreshActiveBoomBoxes`
also calls on every Boom Box). It's local to each machine, so other players are never affected. The tape list page
may show the last album line instead of the idle text; that's cosmetic.

## Seeking

The mod page's seek slider sends `RequestSeekTo` when released (mouse or gamepad capture end); playback
updates don't move it while dragging. The vanilla progress bar stays display-only: it is a `ProgressBar`
inside a Blueprint, and making it draggable would mean overlaying a slider on it by widget hook — possible
later, but the mod page slider covers the need.

## Personal volume slider

"My Volume" sits on the transport row (`MyVolumeSlider` + `MyVolumeText`, both `BindWidgetOptional`), next
to Repeat. It's a plain UMG `USlider`, given an explicit 0–2 range (`SetMinValue`/`SetMaxValue`) so 1.0
(unboosted) sits at its midpoint and the top half boosts Custom Music above the game's own mix. It reads
and writes the `MusicVolume` mod-config value directly — `UBBPConfig::GetFloat` /
the new `UBBPConfig::SetFloat` — which is a local, unreplicated setting already (see Multiplayer.md), so
this is genuinely a "my volume" control: it never touches anyone else's playback.

`UBBPConfig::SetFloat` finds the live `UConfigPropertyFloat`, writes `Value` directly and calls
`UConfigManager::MarkConfigurationDirty`, which the manager saves to disk on a short debounce timer (it
doesn't write to `BoomBoxPlus.cfg` on every drag frame). `UBBPPlaybackController::UpdateGameVolumeScale`
already reads `MusicVolume` fresh every tick, so a drag audibly changes the volume immediately, before the
disk write happens.

`RefreshTransport()` (called from `NativeTick`, so at least once per frame the page is visible) calls
`RefreshMyVolume()`, which snaps the slider to the live config value and updates the `NN%` label — except
while `bChangingMyVolume` is set (between `OnMouseCaptureBegin`/`OnControllerCaptureBegin` and their `End`
counterparts), so a drag isn't fought by the same per-tick refresh that reads it back. Same pattern as
`bSeeking` for the seek slider, but its own flag: reusing `bSeeking` would have made this slider block seek
bar updates too.

## Link panel

Shows this Boom Box's channel code and how many Boom Boxes share it, a 4-digit code field, Link and Unlink
(Unlink only while shared). The code is requested (`Server_EnsureChannel`) the first time the page is
shown, not only once something plays or is queued, so there's always a real code to read here rather than
a placeholder. The line under it shows a pending link request with its countdown, or the server's last
reply for 8 s. See Multiplayer.md.

## Radio: one search box, no separate panel

A live stream address is just another kind of link the search box understands: `ClassifyLink` returns
`EBBPLinkKind::Stream` for anything `BBPRadio::IsStreamUrl` accepts (v1.2.5; before that, a pasted stream
address bounced out of the search box into a dedicated Radio panel with its own field and buttons — a whole
extra panel for what's really the same "paste a link, press Enter, get a result" flow as everything else).
Pressing Enter on a stream address calls `UBBPNetSubsystem::TuneRadio` and shows the tuned-in station as a
single search result, with the same **Play Now / Play Next / Add** buttons as any other result
(`BBPTrackRow::SetupAsResult` already handles `Track.IsLive()` — the LIVE tag, no duration — regardless of
where the result came from). Tuning errors are full sentences and show as the results message as they are.
Nothing about queuing changes because a station is playing: it's one more track in the same queue, so songs
queue normally alongside it. YouTube live links don't go through `TuneRadio`: they classify as
`YouTubeVideo`, and the yt-dlp listing marks them live.

(1.2.5 shipped with `TuneRadio` still rejecting every link `ClassifyLink` didn't call `None`. That now
included the new `Stream` kind, so pasting a station address always failed with "That's a song or playlist
link". Only the recent-station buttons, which skip `TuneRadio`, worked. Fixed in 1.2.6.)

What's left of the old Radio panel is a row under the search box: a "Recent stations:" label and up to four
compact buttons (newest first, from `UBBPNetSubsystem::GetRecentStations`), which play that station again without
retyping the address. Tuning in successfully (via the search box) is what earns a station a spot here, not
pressing Play Now specifically. The label is hidden while there are no recent stations. Each button has its own
`HandleRecentStationN` handler because `UBBPGameButton::OnClicked` carries no payload. The Link panel, freed
from sharing a row with Radio, is back to a plain full-width panel.

The Now Playing panel has three text lines: the title ("Artist - Title"; for radio, the song the station
announces, or the station name until it announces one), the source ("Source: Radio: <station>", see
`GetSourceName` below) with a **Copy Link** button, and a line of its own for synced lyrics. Copy Link puts the
track's `SourceRef` on the clipboard (`FPlatformApplicationMisc::ClipboardCopy`, module `ApplicationCore`): the
YouTube/SoundCloud page or the station's stream address. It reads "Copied!" for 2 s and is hidden for local
files. Since 1.2.6, `SourceRef` is yt-dlp's `webpage_url` when it gives one: SoundCloud's `url` is an
api.soundcloud.com address that can't be opened in a browser. While a live entry plays, the position text shows
**LIVE**, the seek bar is disabled (duration 0), and the lyric line shows "Connecting to the station..." while the
prebuffer fills, or a note that resuming reconnects. See Radio.md.

## Controls in v1

Search results have **Play Now** (`RequestPlayTrackNow`: insert after the current track and start it), Play Next and Add. Queue reordering uses a narrow column of stacked **^ / v** buttons plus a remove `X` on every row; drag-and-drop can be added in the
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

- the current lyric line, and
- a "NOW PLAYING" card 4 px above it, for 4 s when the track changes (fading out over the last 0.5 s),

stacked in one vertical box whose bottom edge is anchored at (0.5, 0.84), so the gap between them is the same
at every resolution,

**only while the local pawn is within hearing range** of a Boom Box playing Custom Music
(`UBBPPlaybackController::GetAudibleRange()`, the attenuation's inner radius + falloff = 250 m). Both are
toggled by the mod config. Same `BindWidgetOptional` pattern as the page: `LyricText`, `NowPlayingBox`,
`NowPlayingTitleText`, `NowPlayingArtistText`.

**The card used to sit top-right, and got hidden behind the vanilla objective/milestone panel.** The
overlay is added at Z-order -10 (`UBBPPlaybackController::EnsureHudOverlay`), behind the game's own default
HUD layer, and there's no version-safe way from a mod to know how tall that panel will be (more active
milestones make it taller) or what Z-order it uses. Bottom-centre is a screen region the vanilla HUD never
draws into regardless of Z stacking, so both notifications live there now, in the same style, rather than
one being a corner card and the other a bottom line.

It stays visible in the pause menu for now.

## Not done yet

- Full gamepad focus (Blueprint pass).
- Hiding the overlay in menus.
- A draggable vanilla progress bar (see Seeking).
