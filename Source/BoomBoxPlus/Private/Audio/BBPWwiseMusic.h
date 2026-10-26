#pragma once

#include "CoreMinimal.h"

class UBBPStreamingSoundWave;

// Custom Music through the game's Wwise: each Boom Box's music plays as a Wwise Audio Input sound on the Boom Box's own
// Wwise emitter, so the game's rooms, reverb, occlusion and Boom Box mix apply to it. The sound comes from a bank built
// at runtime from one of the game's own Boom Box song banks, with the song's source swapped for Audio Input and every
// object given a new id. The Audio Input callbacks pull audio from the same streaming wave Unreal would play.
namespace BBPWwiseMusic
{
	// Builds and loads the bank and registers the Audio Input callbacks, once per game. Returns true if music can play
	// through Wwise.
	bool EnsureReady();

	// Plays Wave through Wwise on the game object of Emitter (an AkComponent). Returns the playing id, or 0 if Wwise
	// wouldn't play it.
	uint32 Play(UBBPStreamingSoundWave* Wave, UObject* Emitter, int32 SampleRate, int32 Channels);

	void Stop(uint32 PlayingId);

	// Multiplier on the samples (Boom Box volume x My Volume x ducking; 0 mutes). The game's volume sliders apply in Wwise.
	void SetGain(uint32 PlayingId, float Gain);

	// While paused, silence is handed to Wwise and the stream doesn't advance.
	void SetPaused(uint32 PlayingId, bool bPaused);

	// True once Wwise ended this playing id itself (for example when the game stopped every sound on the Boom Box).
	bool HasEnded(uint32 PlayingId);

	// Moves Emitter's Wwise position, room and reverb volumes to where its component is now.
	void UpdatePosition(UObject* Emitter);
}
