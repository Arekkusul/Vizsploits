#include "lesson.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LESSONS 32

static const lesson_t *g_lessons[MAX_LESSONS];
static int g_num_lessons = 0;
static bool g_initialized = false;

void lesson_init(void) {
    g_initialized = true;
}

void lesson_cleanup(void) {
    g_num_lessons = 0;
    g_initialized = false;
}

void lesson_register(const lesson_t *lesson) {
    if (!lesson || g_num_lessons >= MAX_LESSONS) return;
    g_lessons[g_num_lessons++] = lesson;
}

const lesson_t *lesson_find(const char *exploit_name) {
    if (!exploit_name) return NULL;
    for (int i = 0; i < g_num_lessons; i++) {
        if (g_lessons[i] && g_lessons[i]->exploit_name &&
            strcmp(g_lessons[i]->exploit_name, exploit_name) == 0) {
            return g_lessons[i];
        }
    }
    return NULL;
}

const lesson_annotation_t *lesson_get_annotation(const lesson_t *lesson, int step) {
    if (!lesson) return NULL;
    for (size_t i = 0; i < lesson->num_annotations; i++) {
        if (lesson->annotations[i].step_number == step) {
            return &lesson->annotations[i];
        }
    }
    return NULL;
}

const lesson_annotation_t *lesson_get_annotation_by_type(const lesson_t *lesson,
                                                          primitive_type_t type) {
    if (!lesson || type == PRIM_NONE) return NULL;
    for (size_t i = 0; i < lesson->num_annotations; i++) {
        if (lesson->annotations[i].step_number == -1 &&
            lesson->annotations[i].trigger_type == type) {
            return &lesson->annotations[i];
        }
    }
    return NULL;
}

int lesson_count(void) {
    return g_num_lessons;
}

const lesson_t *lesson_get_by_index(int index) {
    if (index < 0 || index >= g_num_lessons) return NULL;
    return g_lessons[index];
}
