/* audio_dos.cpp
 *
 * MS-DOS audio on Allegro 4, mirroring audio_a5.cpp:
 *
 *  - Digital samples (voices, battle sounds) on Sound Blaster voices.
 *  - AdLib music and synth sound effects on the real OPL2/OPL3 chip,
 *    run from a 72 Hz timer interrupt as the original game did.
 *  - MIDI music (General MIDI, MT-32) on an MPU-401, run from a 120 Hz
 *    timer interrupt; synth effects stay on the OPL meanwhile.
 *
 * Interrupt work is the ADL driver step only (a few OPL register
 * writes); sample mixing is Allegro's.
 */

#include <allegro.h>
#include <assert.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include "buildcfg.h"

#include "audio_a5.h"

#include "adl/sound_adlib.h"
#include <allegro5/allegro_memfile.h>

extern bool OPLHW_Detect(void);

extern "C" {
#include "errorlog.h"
#include "../crashlog/runlog_dos.h"
#include "../timer/perf_dos.h"

#include "audio.h"
#include "midi.h"
#include "../common_dos.h"
#include "../dos/config_dos.h"
#include "mt32mpu.h"
#include "../file.h"
#include "../table/sound.h"
}

/* Sample instance 0 for narrator voices.
 * Sample instance 1 for acknowledgements.
 * Other instances for general battle sounds.
 */
#define MAX_SAMPLE_INSTANCES 12

enum MusicStreamType {
	MUSICSTREAM_NONE,
	MUSICSTREAM_ADLIB,
	MUSICSTREAM_MIDI,
};

int g_dos_sound_voices = MAX_SAMPLE_INSTANCES;
int g_dos_mixer_quality = 3;

/* Voices actually used: slot rules below are clamped to this. */
static int s_num_voices = MAX_SAMPLE_INSTANCES;

static const int ADLIB_TICKS_PER_SECOND = 72;
static const int MIDI_TICKS_PER_SECOND = 120;

static enum MusicStreamType curr_music_stream_type;
static bool s_have_digi;
static bool s_have_opl;

/* The ADL player; music and synth effects share it (one OPL). */
static SoundAdLibPC * volatile s_adlib;
static bool s_adlib_is_music;

/* MIDI music: the song data and MPU player state live until it stops. */
static bool s_have_midi;
static volatile bool s_midi_playing;
static uint8 *s_midi_file;
static uint8 *s_midi_buffer;
static uint16 s_midi_index = 0xFFFF;

static SAMPLE *s_sample[SAMPLEID_MAX];
static int s_voice[MAX_SAMPLE_INSTANCES];
static SAMPLE *s_voice_sample[MAX_SAMPLE_INSTANCES];

/*--------------------------------------------------------------*/

static void
AudioDOS_MidiInterrupt(void)
{
	if (s_midi_playing) {
		const uint64_t t0 = Perf_Now();
		MPU_Interrupt();
		Perf_Add(PERF_IRQ_MUSIC, (uint32_t)(Perf_Now() - t0));
	}
}
END_OF_STATIC_FUNCTION(AudioDOS_MidiInterrupt)

static void
AudioDOS_StopMidi(void)
{
	if (s_midi_index == 0xFFFF && s_midi_file == NULL)
		return;

	disable();
	s_midi_playing = false;
	enable();

	MPU_Uninit();

	delete[] s_midi_buffer;
	delete[] s_midi_file;
	s_midi_buffer = NULL;
	s_midi_file = NULL;
	s_midi_index = 0xFFFF;
}

static void
AudioDOS_AdlibInterrupt(void)
{
	SoundAdLibPC *adlib = s_adlib;

	if (adlib != NULL) {
		const uint64_t t0 = Perf_Now();
		adlib->tick();
		Perf_Add(PERF_IRQ_MUSIC, (uint32_t)(Perf_Now() - t0));
	}
}
END_OF_STATIC_FUNCTION(AudioDOS_AdlibInterrupt)

/* Replace the ADL player, keeping the timer interrupt out of it. */
static void
AudioDOS_SetAdlib(SoundAdLibPC *adlib, bool is_music)
{
	disable();
	SoundAdLibPC *old = s_adlib;
	s_adlib = adlib;
	s_adlib_is_music = is_music;
	enable();

	delete old;
}

static void
AudioDOS_DisableMusicSet(enum MusicSet music_set)
{
	for (int musicID = MUSIC_STOP; musicID < MUSICID_MAX; musicID++) {
		MusicList *l = &g_table_music[musicID];

		for (int s = 0; s < l->length; s++) {
			if (l->song[s].music_set == music_set)
				l->song[s].enable &=~MUSIC_FOUND;
		}
	}
}

static uint8 *
AudioDOS_LoadFile(const char *filename, uint32 *ret_length)
{
	const uint16 file_index = File_Open(filename, FILE_MODE_READ);
	if (file_index == FILE_INVALID)
		return NULL;

	const uint32 length = File_GetSize(file_index);
	uint8 *buf = new uint8[length];

	File_Read(file_index, buf, length);
	File_Close(file_index);

	*ret_length = length;
	return buf;
}

static SoundAdLibPC *
AudioDOS_CreateAdlib(const MusicInfo *mid)
{
	uint32 length;
	uint8 *buf = AudioDOS_LoadFile(mid->filename, &length);
	if (buf == NULL)
		return NULL;

	/* Only one OPL instance may exist: drop the old player first. */
	AudioDOS_SetAdlib(NULL, false);

	ALLEGRO_FILE *f = al_open_memfile(buf, length, "r");
	SoundAdLibPC *adlib = new SoundAdLibPC(f, ADLIB_TICKS_PER_SECOND, false);
	al_fclose(f);
	delete[] buf;

	adlib->init();
	return adlib;
}

static void
AudioDOS_InitAdlibEffects(void)
{
	if (!s_have_opl || s_adlib != NULL)
		return;

	SoundAdLibPC *adlib = AudioDOS_CreateAdlib(&g_table_music[MUSIC_IDLE1].song[0]);
	if (adlib != NULL)
		AudioDOS_SetAdlib(adlib, false);
}

void
AudioA5_Init(void)
{
	for (int i = 0; i < MAX_SAMPLE_INSTANCES; i++)
		s_voice[i] = -1;

	/* -nosound: leave all sound hardware alone (for troubleshooting). */
	if (DOS_HasArg("nosound")) {
		RunLog_Printf("Sound: disabled (-nosound)\n");
		AudioDOS_DisableMusicSet(MUSICSET_DUNE2_ADLIB);
		AudioDOS_DisableMusicSet(MUSICSET_DUNE2_MIDI);
		AudioDOS_DisableMusicSet(MUSICSET_FLUIDSYNTH);
		g_enable_audio = false;
		g_enable_subtitles = true;
		return;
	}

	/* Digital samples: Sound Blaster etc., from the BLASTER variable.
	 * Every voice costs mixing time in the Sound Blaster interrupt:
	 * Dune II played one sample at a time, Dune Dynasty up to 12. */
	const char *arg = DOS_ArgValue("voices");
	s_num_voices = MID(1, (arg != NULL) ? atoi(arg) : g_dos_sound_voices, MAX_SAMPLE_INSTANCES);

	reserve_voices(s_num_voices, 0);
	set_volume_per_voice(1);

	if (install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL) == 0) {
		s_have_digi = (digi_driver != NULL && digi_driver->id != DIGI_NONE);
	}

	if (s_have_digi) {
		/* Interpolating into a 16-bit buffer is costly on a 486. */
		const int quality = (g_dos_mixer_quality <= 2) ? g_dos_mixer_quality : ((cpu_family >= 5) ? 2 : 0);
		set_mixer_quality(quality);

		RunLog_Printf("Sound: %s (%s), %d voices, mixer quality %d\n",
			digi_driver->name, digi_driver->desc, s_num_voices, get_mixer_quality());
	} else {
		Warning("No digital sound: %s\n", allegro_error);
	}

	/* AdLib music and effects on the real chip. */
	s_have_opl = OPLHW_Detect();
	RunLog_Printf("Music: %s\n", s_have_opl ? "OPL (AdLib) detected at 388h" : "no OPL detected");

	if (s_have_opl) {
		LOCK_FUNCTION(AudioDOS_AdlibInterrupt);
		LOCK_VARIABLE(s_adlib);
		install_int_ex(AudioDOS_AdlibInterrupt, BPS_TO_TIMER(ADLIB_TICKS_PER_SECOND));
	} else {
		AudioDOS_DisableMusicSet(MUSICSET_DUNE2_ADLIB);
	}

	/* General MIDI / MT-32 music on an MPU-401. */
	if (g_table_music_set[MUSICSET_DUNE2_MIDI].enable) {
		s_have_midi = midi_init();
		if (!s_have_midi)
			RunLog_Printf("MIDI: no MPU-401 answered (port from BLASTER P, else 330h)\n");
	} else {
		RunLog_Printf("MIDI: off (music set disabled in DUNEDYN.CFG)\n");
	}

	if (s_have_midi) {
		RunLog_Printf("MIDI: MPU-401 found, %s music\n", (g_midi_format == MIDI_FORMAT_MT32) ? "MT-32" : "General MIDI");
		LOCK_FUNCTION(AudioDOS_MidiInterrupt);
		install_int_ex(AudioDOS_MidiInterrupt, BPS_TO_TIMER(MIDI_TICKS_PER_SECOND));
	} else {
		AudioDOS_DisableMusicSet(MUSICSET_DUNE2_MIDI);
	}

	AudioDOS_DisableMusicSet(MUSICSET_FLUIDSYNTH);

	g_enable_audio = s_have_digi || s_have_opl || s_have_midi;
	if (!s_have_digi)
		g_enable_subtitles = true;

	AudioDOS_InitAdlibEffects();
}

void
AudioA5_Uninit(void)
{
	if (s_have_opl)
		remove_int(AudioDOS_AdlibInterrupt);

	AudioDOS_StopMidi();
	if (s_have_midi) {
		remove_int(AudioDOS_MidiInterrupt);
		midi_uninit();
	}

	AudioDOS_SetAdlib(NULL, false);
	curr_music_stream_type = MUSICSTREAM_NONE;

	for (int i = 0; i < MAX_SAMPLE_INSTANCES; i++) {
		if (s_voice[i] >= 0)
			deallocate_voice(s_voice[i]);
		s_voice[i] = -1;
	}

	for (int sampleID = 0; sampleID < SAMPLEID_MAX; sampleID++) {
		if (s_sample[sampleID] != NULL)
			destroy_sample(s_sample[sampleID]);
		s_sample[sampleID] = NULL;
	}

	remove_sound();
}

/*--------------------------------------------------------------*/

/* As AudioA5_LoadInternalMusic: the MIDI set has a file per format. */
static uint8 *
AudioDOS_LoadMidiFile(const MusicInfo *mid, uint32 *length)
{
	char filename[64];
	snprintf(filename, sizeof(filename), "%s", mid->filename);

	const size_t len = strlen(filename);
	const char *ext = NULL;

	if (g_midi_format == MIDI_FORMAT_PCS) ext = "PCS";
	else if (g_midi_format == MIDI_FORMAT_TAN) ext = "TAN";
	else if (g_midi_format == MIDI_FORMAT_MT32) ext = "XMI";

	if (ext != NULL && len >= 3)
		memcpy(filename + len - 3, ext, 3);

	return AudioDOS_LoadFile(filename, length);
}

static void
AudioDOS_InitMidiMusic(const MusicInfo *mid)
{
	AudioDOS_StopMidi();

	/* The ADL player stays for synth sound effects. */
	if (s_adlib_is_music)
		AudioDOS_SetAdlib(NULL, false);
	AudioDOS_InitAdlibEffects();

	uint32 length;
	s_midi_file = AudioDOS_LoadMidiFile(mid, &length);
	if (s_midi_file == NULL) {
		curr_music_stream_type = MUSICSTREAM_NONE;
		return;
	}

	MPU_Init();
	s_midi_buffer = new uint8[MPU_GetDataSize()];
	s_midi_index = MPU_SetData(s_midi_file, mid->track, s_midi_buffer);
	MPU_Play(s_midi_index);
	MPU_SetVolume(s_midi_index, 100 * music_volume, 0);

	curr_music_stream_type = MUSICSTREAM_MIDI;
	s_midi_playing = true;
}

void
AudioA5_InitInternalMusic(const MusicInfo *mid)
{
	if (mid->music_set == MUSICSET_DUNE2_MIDI) {
		if (s_have_midi)
			AudioDOS_InitMidiMusic(mid);
		return;
	}

	if (mid->music_set != MUSICSET_DUNE2_ADLIB || !s_have_opl)
		return;

	AudioDOS_StopMidi();

	SoundAdLibPC *adlib = AudioDOS_CreateAdlib(mid);
	if (adlib == NULL) {
		curr_music_stream_type = MUSICSTREAM_NONE;
		AudioDOS_InitAdlibEffects();
		return;
	}

	adlib->playTrack(mid->track);
	AudioDOS_SetAdlib(adlib, true);
	curr_music_stream_type = MUSICSTREAM_ADLIB;
}

void
AudioA5_InitExternalMusic(const MusicInfo *ext)
{
	/* No FLAC/MP3/Ogg/AUD streaming on MS-DOS (yet). */
	VARIABLE_NOT_USED(ext);
	curr_music_stream_type = MUSICSTREAM_NONE;
}

void
AudioA5_SetMusicVolume(float volume)
{
	/* The OPL has no master volume; MIDI does. */
	if (curr_music_stream_type == MUSICSTREAM_MIDI && s_midi_index != 0xFFFF) {
		disable();
		MPU_SetVolume(s_midi_index, volume == 1.0f ? MPU_MAX_VOLUME : ((uint16)(volume * 100) * 90) / 256, 0);
		enable();
	}
}

void
AudioA5_StopMusic(void)
{
	/* Keep the player for sound effects, as Allegro 5 does. */
	if (curr_music_stream_type == MUSICSTREAM_ADLIB && s_adlib != NULL)
		s_adlib->haltTrack();

	if (curr_music_stream_type == MUSICSTREAM_MIDI) {
		AudioDOS_StopMidi();
		curr_music_stream_type = MUSICSTREAM_NONE;
	}
}

void
AudioA5_PollMusic(void)
{
	/* The players run from timer interrupts; free a finished MIDI song. */
	if (curr_music_stream_type == MUSICSTREAM_MIDI && s_midi_index != 0xFFFF
			&& MPU_IsPlaying(s_midi_index) != 1) {
		AudioDOS_StopMidi();
		curr_music_stream_type = MUSICSTREAM_NONE;
	}
}

bool
AudioA5_MusicIsPlaying(void)
{
	if (curr_music_stream_type == MUSICSTREAM_MIDI)
		return s_midi_index != 0xFFFF && MPU_IsPlaying(s_midi_index) == 1;

	if (curr_music_stream_type != MUSICSTREAM_ADLIB || s_adlib == NULL || !s_adlib_is_music)
		return false;

	return s_adlib->isPlaying();
}

void
AudioA5_PlaySoundEffect(enum SoundID effectID)
{
	if (s_adlib != NULL)
		s_adlib->playSoundEffect(effectID);
}

/*--------------------------------------------------------------*/

void
AudioA5_StoreSample(enum SampleID sampleID, uint8 file_index, uint32 file_size)
{
	char header[0x1A];
	char ignore1;
	unsigned int size = 0;
	unsigned char rate;
	char ignore2;

	/* Creative Voice File, one block of unsigned 8-bit mono, as read by
	 * the Allegro 5 backend. */
	File_Read(file_index, header, 0x1A);
	File_Read(file_index, &ignore1, 1);
	File_Read(file_index, &size, 3);
	File_Read(file_index, &rate, 1);
	File_Read(file_index, &ignore2, 1);

	const unsigned int freq = 1000000 / (256 - rate);
	const unsigned int len = (size >= 2) ? size - 2 : 0;
	const unsigned int avail = (file_size > 32) ? file_size - 32 : 0;

	if (s_sample[sampleID] != NULL) {
		/* Stop voices still playing the old sample. */
		for (int i = 0; i < MAX_SAMPLE_INSTANCES; i++) {
			if (s_voice[i] >= 0 && s_voice_sample[i] == s_sample[sampleID]) {
				deallocate_voice(s_voice[i]);
				s_voice[i] = -1;
				s_voice_sample[i] = NULL;
			}
		}

		destroy_sample(s_sample[sampleID]);
		s_sample[sampleID] = NULL;
	}

	SAMPLE *spl = create_sample(8, FALSE, freq, MIN(len, avail));
	if (spl == NULL)
		return;

	File_Read(file_index, spl->data, spl->len);
	s_sample[sampleID] = spl;
}

bool
AudioA5_PlaySample(enum SampleID sampleID, float volume, float pan)
{
	int idx_start, idx_end;
	float gain;

	if (s_sample[sampleID] == NULL)
		return true;

	if (SAMPLE_BLASTER <= sampleID && sampleID < SAMPLEID_MAX) {
		idx_start = 0;
		idx_end = 0;
		gain = cutscene_sound_volume * volume;
		pan = -1000.0f;
	} else if ((SAMPLE_VOICE_FRAGMENT_ENEMY <= sampleID && sampleID <= SAMPLE_VOICE_FRAGMENT_YOUR_NEXT_CONQUEST) ||
	    (sampleID == SAMPLE_RADAR_STATIC)) {
		idx_start = 0;
		idx_end = 0;
		gain = sound_volume * volume;
		pan = -1000.0f;
	} else if (SAMPLE_AFFIRMATIVE <= sampleID && sampleID <= SAMPLE_MOVING_OUT) {
		idx_start = 1;
		idx_end = 1;
		gain = sound_volume * volume;
	} else {
		idx_start = 2;
		idx_end = MAX_SAMPLE_INSTANCES - 1;
		gain = sound_volume * volume;
	}

	return AudioA5_PlaySampleRaw(sampleID, gain, pan, idx_start, idx_end);
}

static bool
AudioDOS_VoicePlaying(int slot)
{
	return (s_voice[slot] >= 0) && (voice_get_position(s_voice[slot]) >= 0);
}

bool
AudioA5_PlaySampleRaw(enum SampleID sampleID, float volume, float pan, int idx_start, int idx_end)
{
	if (!s_have_digi || s_sample[sampleID] == NULL)
		return true;

	/* With fewer voices, the narrator, acknowledgement and battle slots
	 * share what there is. */
	idx_start = MIN(idx_start, s_num_voices - 1);
	idx_end = MIN(idx_end, s_num_voices - 1);

	for (int i = idx_start; i <= idx_end; i++) {
		if (AudioDOS_VoicePlaying(i))
			continue;

		if (s_voice[i] >= 0)
			deallocate_voice(s_voice[i]);

		s_voice[i] = allocate_voice(s_sample[sampleID]);
		s_voice_sample[i] = s_sample[sampleID];
		if (s_voice[i] < 0)
			continue;

		const int vol = (int)(255.0f * volume);
		const int pan255 = (pan < -100.0f) ? 128 : (int)(127.5f * (pan + 1.0f));

		voice_set_volume(s_voice[i], MID(0, vol, 255));
		voice_set_pan(s_voice[i], MID(0, pan255, 255));
		voice_start(s_voice[i]);
		return true;
	}

	return false;
}

/* For the performance overlay. */
extern "C" void
AudioDOS_CountVoices(int *playing, int *total)
{
	*playing = 0;
	*total = s_have_digi ? s_num_voices : 0;

	for (int i = 0; i < *total; i++) {
		if (AudioDOS_VoicePlaying(i))
			(*playing)++;
	}
}

bool
AudioA5_PollNarrator(void)
{
	return AudioDOS_VoicePlaying(0);
}
