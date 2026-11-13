#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY

// Allow two queued pages per supported voice, plus load, seek and cleanup jobs.
#define MA_JOB_TYPE_RESOURCE_MANAGER_QUEUE_CAPACITY 8192
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stb_vorbis.c>
