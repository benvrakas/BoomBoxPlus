# BoomBoxPlus

A Satisfactory mod (for game build 502094 / 1.2.4, SML 3.12) that makes the Boom Box play your own music —
plus YouTube and SoundCloud — synced for everyone in the session. Inspired by the PEAK mod sPEAKer.

- Put `.mp3`, `.ogg` or `.wav` files in `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\Music`.
- Load the **Custom Music** tape into a Boom Box to open the music page: search, queue (+Front / +End),
  shuffle, repeat.
- Everyone within earshot hears the same track at the same position; lyrics and a "now playing" card
  appear on screen.

**Status:** in development — builds, not yet tested in-game.

Developer documentation is in [`Docs/`](Docs/Architecture.md).

## Credits

- Demo track: "Monkeys Spinning Monkeys" Kevin MacLeod (incompetech.com). Licensed under Creative Commons:
  By Attribution 4.0 License — http://creativecommons.org/licenses/by/4.0/
- Audio decoders: [dr_mp3 and dr_wav](https://github.com/mackron/dr_libs) (public domain / MIT-0),
  [stb_vorbis](https://github.com/nothings/stb) (public domain).
- Synced lyrics: [LRCLIB](https://lrclib.net).
