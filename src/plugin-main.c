#include "obs-module.h"
#include "plugin-support.h"
#include <string.h>
#include <stdint.h>

typedef uint_fast64_t flags_int;

#define AUDIO_SOURCE_MIXER_ID "audio_source_mixer"
//TODO: Investigate the common maximum block size
#define AUDIO_FRAMES_MAX 0x2000
#define NUM_OF_SOURCES sizeof(flags_int) * CHAR_BIT

const char *S_SOURCE[NUM_OF_SOURCES] = {
	"source1", "source2", "source3", "source4", "source5", "source6", "source7", "source8",
	"source9", "source10", "source11", "source12", "source13", "source14", "source15", "source16",
	"source17", "source18", "source19", "source20", "source21", "source22", "source23", "source24",
	"source25", "source26", "source27", "source28", "source29", "source30", "source31", "source32",
	"source33", "source34", "source35", "source36", "source37", "source38", "source39", "source40",
	"source41", "source42", "source43", "source44", "source45", "source46", "source47", "source48",
	"source49", "source50", "source51", "source52", "source53", "source54", "source55", "source56",
	"source57", "source58", "source59", "source60", "source61", "source62", "source63", "source64",
};

const char *TEXT_SOURCE[NUM_OF_SOURCES] = {
	"Source 1", "Source 2", "Source 3", "Source 4", "Source 5", "Source 6", "Source 7", "Source 8",
	"Source 9", "Source 10", "Source 11", "Source 12", "Source 13", "Source 14", "Source 15",
	"Source 16", "Source 17", "Source 18", "Source 19", "Source 20", "Source 21", "Source 22",
	"Source 23", "Source 24", "Source 25", "Source 26", "Source 27", "Source 28", "Source 29",
	"Source 30", "Source 31", "Source 32", "Source 33", "Source 34", "Source 35", "Source 36",
	"Source 37", "Source 38", "Source 39", "Source 40", "Source 41", "Source 42", "Source 43",
	"Source 44", "Source 45", "Source 46", "Source 47", "Source 48", "Source 49", "Source 50",
	"Source 51", "Source 52", "Source 53", "Source 54", "Source 55", "Source 56", "Source 57",
	"Source 58", "Source 59", "Source 60", "Source 61", "Source 62", "Source 63", "Source 64",
};

struct data;

struct audio_capture_cb_param {
	obs_source_t *source;
	struct data *data;
	int index;
	uint32_t written_frames;
};

struct data {
	float audio_buf[MAX_AV_PLANES][AUDIO_FRAMES_MAX];
	struct audio_capture_cb_param cb_params[NUM_OF_SOURCES];
	struct obs_source_audio audio;
	obs_source_t *context;
	// Flags that bit N is 1 if source N's frame is not written to audio_buf, otherwise 0.
	flags_int frame_waiting_flags;
	// Default state of the flags.
	flags_int frame_waiting_flags_;
};

static void update(void *, obs_data_t *);
static void on_source_mute(void *, calldata_t *);
static void on_source_remove(void *, calldata_t *);
static void on_source_destroy(void *, calldata_t *);

static const char *get_name(void *type_data)
{
	return "Audio Mixer";
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	struct data *data = bzalloc(sizeof(struct data));
	const struct audio_output_info *info = audio_output_get_info(obs_get_audio());

	//memset(data->audio_buf, 0, sizeof(data->audio_buf));
	for (int i = 0; i < NUM_OF_SOURCES; i++) {
		//data->cb_params[i].source = NULL;
		data->cb_params[i].data = data;
		//data->cb_params[i].written_frames = 0;
		data->cb_params[i].index = i;
	}
	for (int c = 0; c < MAX_AV_PLANES; c++) {
		data->audio.data[c] = (uint8_t *)data->audio_buf[c];
	}
	data->audio.frames = AUDIO_FRAMES_MAX;
	data->audio.speakers = info->speakers;
	data->audio.format = info->format;
	data->audio.samples_per_sec = info->samples_per_sec;
	//data->audio.timestamp = 0;
	data->context = source;
	//data->frame_waiting_flags = 0;
	//data->frame_waiting_flags_ = 0;
	return data;
}

static void data_output_audio(struct data *data)
{
	obs_source_output_audio(data->context, &data->audio);

	uint32_t remain_frames = AUDIO_FRAMES_MAX - data->audio.frames;
	size_t remain_bytes = sizeof(float)*remain_frames;
	size_t output_bytes = sizeof(float)*data->audio.frames;
	for (int c = 0; c < MAX_AV_PLANES; c++) {
		float *adata = data->audio_buf[c];
		memmove(adata, adata+data->audio.frames, remain_bytes);
		memset(adata+remain_frames, 0, output_bytes);
	}
	data->frame_waiting_flags = data->frame_waiting_flags_;
	for (int i = 0; i < NUM_OF_SOURCES; i++) {
		uint32_t f = data->audio.frames;
		uint32_t wf = data->cb_params[i].written_frames;
		data->cb_params[i].written_frames = (wf > f) * (wf - f);

		// Unset a flag of a source if some frames of the source still remain.
		data->frame_waiting_flags &= ~((wf > f) << i);
	}
	data->audio.timestamp = 0;
	data->audio.frames = AUDIO_FRAMES_MAX;
}

static void audio_capture_cb(void *param_, obs_source_t *source, const struct audio_data *audio_data, bool muted)
{
	if (muted)
		return;

	struct audio_capture_cb_param *param = param_;
	struct data *data = param->data;
	uint_fast64_t flag_mask = 1ULL << param->index;
	data->frame_waiting_flags |= flag_mask;

	float k = obs_source_get_volume(param->source);
	for (int c = 0; c < MAX_AV_PLANES; c++) {
		float *adata_ = (float *)audio_data->data[c];
		if (!adata_)
			continue;

		float *adata = data->audio_buf[c];
		for (uint32_t i = param->written_frames, j = 0; j < audio_data->frames; i++, j++)
			adata[i] += k*adata_[j];
	}
	param->written_frames += audio_data->frames;
	data->audio.timestamp = audio_data->timestamp;
	data->audio.frames = (param->written_frames < data->audio.frames)
				     ? param->written_frames
				     : data->audio.frames;

	data->frame_waiting_flags &= ~flag_mask;
	if (data->frame_waiting_flags == 0)
		data_output_audio(data);
}

static void on_source_remove(void *param_, calldata_t *cd)
{
	struct audio_capture_cb_param *param = param_;

	obs_source_remove_audio_capture_callback(param->source, audio_capture_cb, param);
	signal_handler_t *h = obs_source_get_signal_handler(param->source);
	signal_handler_disconnect(h, "mute", on_source_mute, param);
	signal_handler_disconnect(h, "remove", on_source_remove, param);
	signal_handler_disconnect(h, "destroy", on_source_destroy, param);
	param->source = NULL;
}

static void on_source_destroy(void *param_, calldata_t *cd)
{
	struct audio_capture_cb_param *param = param_;
	struct data *data = param->data;

	obs_data_t *settings = obs_source_get_settings(data->context);
	obs_data_set_string(settings, S_SOURCE[param->index], "");
	obs_data_release(settings);

	param->source = NULL;

	uint_fast64_t i = 1ULL << param->index;
	data->frame_waiting_flags &= ~i;
	data->frame_waiting_flags_ &= ~i;
	if (data->frame_waiting_flags == 0)
		data_output_audio(data);
}

static void on_source_mute(void *param_, calldata_t *cd)
{
	struct audio_capture_cb_param *param = param_;
	struct data *data = param->data;

	uint_fast64_t i = 1ULL << param->index;
	if (calldata_bool (cd, "muted")) {
		data->frame_waiting_flags &= ~i;
		data->frame_waiting_flags_ &= ~i;
	} else {
		data->frame_waiting_flags |= i;
		data->frame_waiting_flags_ |= i;
	}
	if (data->frame_waiting_flags == 0)
		data_output_audio(data);
}

static void reset_sources_callbacks(struct data *data)
{
	for (int i = 0; i < NUM_OF_SOURCES; i++) {
		obs_source_t *s = data->cb_params[i].source;
		if (s) {
			struct audio_capture_cb_param *p = &data->cb_params[i];
			obs_source_remove_audio_capture_callback(s, audio_capture_cb, p);
			signal_handler_t *h = obs_source_get_signal_handler(s);
			signal_handler_disconnect(h, "mute", on_source_mute, p);
			signal_handler_disconnect(h, "remove", on_source_remove, p);
			signal_handler_disconnect(h, "destroy", on_source_destroy, p);
			data->cb_params[i].source = NULL;
		}
	}
}

static void destroy(void *data_)
{
	reset_sources_callbacks((struct data *)data_);
	bfree(data_);
}

struct uuid_source {
	const char *uuid;
	obs_source_t *source;
};

static bool uuid_to_source(void *param_, obs_source_t *source)
{
	struct uuid_source *param = param_;
	const char *uuid = obs_source_get_uuid(source);
	for (int i = 0; i < NUM_OF_SOURCES; i++)
		if (!strcmp(param[i].uuid, uuid))
			param[i].source = source;

	return true;
}

static void update(void *data_, obs_data_t *settings)
{
	struct data *data = data_;
	struct uuid_source param[NUM_OF_SOURCES] = { {NULL, NULL} };
	for (int i = 0; i < NUM_OF_SOURCES; i++)
		param[i].uuid = obs_data_get_string(settings, S_SOURCE[i]);

	obs_enum_sources(uuid_to_source, &param);
	data->audio.timestamp = 0;
	data->audio.frames = AUDIO_FRAMES_MAX;
	data->frame_waiting_flags = 0;
	data->frame_waiting_flags_ = 0;

	reset_sources_callbacks(data);
	for (int i = 0; i < NUM_OF_SOURCES; i++) {
		obs_source_t *s = param[i].source;
		if (s) {
			struct audio_capture_cb_param *p = &data->cb_params[i];
			data->cb_params[i].source = s;
			signal_handler_t *h = obs_source_get_signal_handler(s);
			signal_handler_connect(h, "mute", on_source_mute, p);
			signal_handler_connect(h, "remove", on_source_remove, p);
			signal_handler_connect(h, "destroy", on_source_destroy, p);
			data->frame_waiting_flags_ |= !obs_source_muted(s) << i;
			data->frame_waiting_flags = data->frame_waiting_flags_;
			obs_source_add_audio_capture_callback(s, audio_capture_cb, p);
		}
	}
}

static void load(void *data, obs_data_t *settings)
{
	update(data, settings);
}

struct add_sources_param {
	obs_property_t *source_list[NUM_OF_SOURCES];
	obs_source_t *context;
};

static bool add_sources(void *param_, obs_source_t *source)
{
	struct add_sources_param *param = param_;

	if (!(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
		return true;
	if (param->context == source)
		return true;

	const char *name = obs_source_get_name(source);
	const char *uuid = obs_source_get_uuid(source);
	for (int i = 0; i < NUM_OF_SOURCES; i++)
		obs_property_list_add_string(param->source_list[i], name, uuid);

	return true;
}

static obs_properties_t *get_properties2(void *data_, void *type_data)
{
	struct data *data = data_;
	obs_properties_t *ppts = obs_properties_create();
	struct add_sources_param param;

	if (data)
		param.context = data->context;

	for (int i = 0; i < NUM_OF_SOURCES; i++) {
		obs_property_t *prop;
		prop = obs_properties_add_list(ppts, S_SOURCE[i], TEXT_SOURCE[i],
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
		param.source_list[i] = prop;
		obs_property_list_add_string(prop, "", "");
	}

	obs_enum_sources(add_sources, &param);

	return ppts;
}

struct obs_source_info audio_source_mixer = {
	.id = AUDIO_SOURCE_MIXER_ID,
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.update = update,
	.load = load,
	.icon_type = OBS_ICON_TYPE_AUDIO_INPUT,
	.get_properties2 = get_properties2,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_register_source(&audio_source_mixer);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
