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
page is told the tape changed.

**One title and subtitle everywhere.** The Custom Music page, the vanilla page and the now-playing toast all show
`UBBPPlaybackController::DescribeNowPlaying` (`FBBPNowPlaying`): a title, and under it "Artist, Source", where
Source is `UBBPBlueprintLibrary::GetSourceName` ("Kind: name"). The artist is left out when it's just the uploader
again, i.e. a video whose title had no "Artist - " part.

| | Title | Subtitle |
|---|---|---|
| YouTube "Koven - Hell Is Where I Dwell" on Monstercat Uncaged | Hell Is Where I Dwell | Koven, YouTube: Monstercat Uncaged |
| YouTube video with no artist in its title | title | YouTube: <channel> |
| SoundCloud | title | SoundCloud: <account> |
| Local file | title | <artist tag>, Local file |
| Radio, song announced ("Artist - Song") | Song | Artist, Radio: <station> |
| Radio, nothing announced yet | station name | Live radio |
| YouTube live | video title | [artist, ] YouTube live: <channel> |

The artist from an "Artist - Title" video title is the mod's own split (`ParseTrackJson`), and the channel is
yt-dlp's `channel`/`uploader` (`FBBPTrack::Uploader`), which is why they can differ.

On the vanilla page the lines are, top to bottom: the tape's title ("Custom Music"), the song line ("4. <title>",
with the queue position), and the album line, which gets the subtitle through the tape's description (the idle
blurb when nothing is queued). The page doesn't show `FSongData::ArtistName` at all (checked in game: setting the
album line to "Custom Music" left no artist anywhere), so the subtitle also goes to ArtistName only for
completeness.

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

"My Volume" sits on the transport row (`MyVolumeSlider` + `MyVolumeBox`, both `BindWidgetOptional`), next
to Repeat. Since 1.3.0 the percentage is an `UEditableTextBox`: click it, type 0-200 (a `%` is ignored) and press
Enter (`HandleMyVolumeCommitted`); anything that isn't a number, or Esc, puts the current value back. The slider
is a plain UMG `USlider`, given an explicit 0–2 range (`SetMinValue`/`SetMaxValue`) so 1.0 (unboosted) sits at
its midpoint and the top half boosts Custom Music above the game's own mix. Since 1.3.2 it and the nearby Boom Box
sliders move in 5% steps (`BBPWidgetStyle::StyleVolumeSlider`: `StepSize` 0.05 with `MouseUsesStep`, and
`SnapVolume` rounds each change, since the step alone only applies to mouse drags). A typed number isn't rounded.

**Per Boom Box, per player** (1.3.0). The value is this player's volume for the Boom Box whose page is open, so two
players with Boom Boxes side by side can each turn the other's down. It lives in `UBBPPlaybackController`
(`GetMyVolume`/`SetMyVolume`), keyed by the **server's** `ABBPPlaylistSubsystem::GetBoomBoxKey` for the Boom Box
(the same save-stable key queue saving uses), which the server sends along in `FBBPActiveBoomBox::Key`. It is never
sent back to the server. Clients can't use their own copy's name: a client re-creates a replicated actor, with a new
generated name, whenever it leaves network range and comes back (a 1.3.0 client log showed the same host Boom Boxes
reappearing as `..._2147438105`, `..._2147405903`, `..._2147364940`), so a volume keyed that way reset without
warning. It's kept between sessions in `Saved/BoomBoxPlus/MyVolumes.json`,
written at most once a second while a slider is dragged and on shutdown; only values other than 100% are stored.
A Boom Box picked up and placed again is a new actor with a new key, so it starts back at 100%, the same way it
gets a new queue. `SyncEmitters` reads it fresh every tick, so a drag is heard immediately. Until 1.2.6 this was one
global value, the hidden `MusicVolume` mod setting; that setting is gone, and SML ignores its leftover entry in old
`BoomBoxPlus.cfg` files.

`RefreshTransport()` (called from `NativeTick`, so at least once per frame the page is visible) calls
`RefreshMyVolume()`, which snaps the slider to the live value and updates the number box (the `%` is a label after it, since 1.3.2) (not while the box
has focus, so typing isn't overwritten) — except
while `bChangingMyVolume` is set (between `OnMouseCaptureBegin`/`OnControllerCaptureBegin` and their `End`
counterparts), so a drag isn't fought by the same per-tick refresh that reads it back. Same pattern as
`bSeeking` for the seek slider, but its own flag: reusing `bSeeking` would have made this slider block seek
bar updates too.

## Boom Boxes you can hear

Under the queue, `NearbySection` lists every Custom Music Boom Box within hearing range of the local player
(`UBBPPlaybackController::GetAudibleBoomBoxes`, nearest first, at most 6), refreshed four times a second. Each
`UBBPNearbyBoomBoxRow` shows "This Boom Box" or "Boom Box <link code>", the distance and what it's playing, with a
0-200% slider that is the same per-player value as that Boom Box's My Volume. The header has a **Hear:** button that
cycles this player's listening mode (see Multiplayer.md, Volume), and a row the mode mutes says so. It exists because My Volume belongs
to the Boom Box whose page is open: the 1.3.0 client log that prompted it showed a player turning their own (silent)
Boom Box up and down while the music they heard came from two others. The section is hidden when no Custom Music
Boom Box is in range.

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

**Stations list** (1.3.0; replaced 1.2.5's four "recent stations" buttons). Under the search box, a `UWrapBox`
(`StationsBox`) holds a "Stations:" label and one `UBBPStationButton` per saved station: the station's name, which
plays it now (`UBBPMusicPage::PlayStation`), and an "x" that removes it (`ForgetStation`). The box wraps onto more
lines as stations are added, up to `UBBPNetSubsystem::MaxStations` (12, newest first; the oldest drops off when
it's full). With none saved, the label says how to add one. Station buttons are made on demand by
`RefreshRadioStations` and collapsed rather than destroyed, like the track rows, and each knows its own station,
so there are no per-index handlers (`UBBPGameButton::OnClicked` carries no payload).

A station is saved when a stream address tunes in, and when a live result (a station or a YouTube live stream) is
queued or played from the results (`UBBPTrackRow` calls `UBBPMusicPage::NoteQueued`). Playing a saved station doesn't
move it: `RememberStation` keeps an existing station's place, so the list doesn't reshuffle as it's used. The Link
panel, freed from sharing a row with Radio, is back to a plain full-width panel.

The Now Playing panel has three text lines: the title and the "Artist, Source" subtitle (the same as the vanilla
page and the toast; see "One title and subtitle everywhere" above), the latter with a **Copy Link** button, and a
line of its own for synced lyrics. Copy Link puts the
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
