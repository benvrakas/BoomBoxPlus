#!/usr/bin/env python3
"""Removes Resources/Tools/ (bundled ffmpeg + yt-dlp, ~176 MB) from the server zips RunUAT PackagePlugin
produces. Dedicated servers never run those tools: UBBPPlaybackController, which is the only thing that calls
into ffmpeg/yt-dlp, is never created when GetNetMode() == NM_DedicatedServer (see BBPPlaylistSubsystem.cpp),
and UBBPNetSubsystem::Initialize degrades gracefully (bToolsAvailable = false, a log warning) when they're
missing. Every real client still downloads and plays media itself; the server only ever relays queue state.

Run after packaging (release-full.bat), before uploading:
    python Tools/TrimServerPackages.py

Edits the zips in Saved/ArchivedPlugins/BoomBoxPlus/ in place: the two single-platform server zips, and the
combined BoomBoxPlus.zip's WindowsServer/ and LinuxServer/ entries. The Windows client zip is untouched.
"""
import zipfile
import os
import sys

ARCHIVE_DIR = os.path.join(
    os.path.dirname(__file__), "..", "..", "..", "Saved", "ArchivedPlugins", "BoomBoxPlus"
)

# (zip file name, path prefixes to drop from it)
JOBS = [
    ("BoomBoxPlus-WindowsServer.zip", ["Resources/Tools/"]),
    ("BoomBoxPlus-LinuxServer.zip", ["Resources/Tools/"]),
    ("BoomBoxPlus.zip", ["WindowsServer/Resources/Tools/", "LinuxServer/Resources/Tools/"]),
]


def trim(path, prefixes):
    if not os.path.isfile(path):
        print(f"skip (not found): {path}")
        return
    before = os.path.getsize(path)
    tmp = path + ".trim"
    dropped = 0
    with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            if any(info.filename.startswith(p) for p in prefixes):
                dropped += 1
                continue
            zout.writestr(info, zin.read(info.filename))
    os.replace(tmp, path)
    after = os.path.getsize(path)
    print(f"{os.path.basename(path)}: dropped {dropped} file(s), {before / 1e6:.0f} MB -> {after / 1e6:.0f} MB")


def main():
    archive_dir = os.path.abspath(ARCHIVE_DIR)
    if not os.path.isdir(archive_dir):
        print(f"error: {archive_dir} doesn't exist; run a release build first", file=sys.stderr)
        sys.exit(1)
    for name, prefixes in JOBS:
        trim(os.path.join(archive_dir, name), prefixes)


if __name__ == "__main__":
    main()
