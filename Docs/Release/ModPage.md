<!--
Text for the BoomBoxPlus page on ficsit.app (Satisfactory Mod Repository).
- "Short description" field: the line under SHORT DESCRIPTION (at most 128 characters).
- "Full description" field: the whole of FullDescription.md (this folder), nothing else.
- The two transparency fields: the text under NETWORK ACTIVITY TRANSPARENCY and AI USAGE TRANSPARENCY.
Replace the [bracketed] placeholders before publishing.
-->

# SHORT DESCRIPTION

The Boom Box plays your music: local files, YouTube, SoundCloud, Spotify playlists and internet radio, synced in multiplayer.

# FULL DESCRIPTION

In FullDescription.md.

# NETWORK ACTIVITY TRANSPARENCY

BoomBoxPlus connects to these services. Multiplayer sync between players uses the game's own connection.

- **YouTube and SoundCloud** (through the bundled yt-dlp): when you search online (press Enter in the
  search box) or paste a YouTube/SoundCloud link, and to download songs that are in a queue you can hear.
  This includes songs other players added to a Boom Box near you: each player's game downloads those songs
  itself.
- **Spotify** (open.spotify.com): only when you paste a Spotify link, to read the song or playlist names.
- **Spotify accounts and Web API** (accounts.spotify.com, api.spotify.com): only if you enter a Spotify
  Client ID and Secret in the settings. Used to read albums and, after you press Connect Spotify and sign
  in, whole playlists. During sign-in the game briefly listens on 127.0.0.1:8888 (your own computer only)
  for Spotify's reply. Your sign-in is stored locally in
  `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\SpotifyLogin.json`.
- **Internet radio stations**: when a radio station is playing on a Boom Box you can hear, your game
  connects to that station's stream address, including stations other players added. When you press Enter
  on a stream address or add a station, your game briefly connects to check the stream and read its name.
  Your saved stations are stored locally in `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\RadioStations.json`.
- **YouTube live streams** (through the bundled yt-dlp): looks up the live stream's address when one plays
  near you, then streams it from YouTube like a radio station.
- **LRCLIB** (lrclib.net): looks up synced lyrics for each song that plays. Sends the song's title, artist
  and length. Turn off "Show lyrics" to stop these requests.

The mod has no telemetry or analytics, and sends nothing anywhere else.

# AI USAGE TRANSPARENCY

[Edit to match how you made the mod.]

This mod was built with the help of an AI coding assistant (Anthropic's Claude):

- **Source code:** most of the mod's C++ source code was written by the AI, directed, reviewed and tested
  in game by the author.
- **Mod page text:** this description was drafted by the AI and edited by the author.
- **Developer documentation** in the source repository was written by the AI.
- **Art and audio:** ADA's easter-egg voice lines are AI-generated speech (Google Cloud Text-to-Speech), written
  by the author. No other AI-generated assets: the UI uses the game's own widgets, fonts and icons, the mod icon
  is a render of the "Satisfactory Boombox" 3D model by Laxity (CC BY 4.0), and the demo track is by Kevin MacLeod.
- **In game:** the mod doesn't use or give access to generative AI.
