#pragma once
#include <stdint.h>
typedef struct { const char *title,*body,*source,*watch; uint8_t category; } orca_story_t;
extern const orca_story_t g_orca_stories[];
extern const uint8_t g_orca_story_count;
