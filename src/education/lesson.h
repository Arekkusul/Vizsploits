#ifndef LESSON_H
#define LESSON_H

#include "../core/primitives.h"
#include <stdbool.h>
#include <stddef.h>

/**
 * Annotation detail level
 */
typedef enum {
    LEVEL_BASIC,
    LEVEL_INTERMEDIATE,
    LEVEL_ADVANCED
} lesson_level_t;

/**
 * A single annotation tied to a step or event type
 */
typedef struct {
    int step_number;              /* -1 means match by trigger_type instead */
    primitive_type_t trigger_type; /* PRIM_NONE means match by step_number */
    const char *title;
    const char *explanation;
    const char *mitigation;
    int related_steps[8];         /* Cross-references, -1 terminated */
    lesson_level_t level;
} lesson_annotation_t;

/**
 * A lesson associated with an exploit
 */
typedef struct {
    const char *exploit_name;     /* Must match exploit_t.meta.name */
    const char *overview;         /* Introductory text */
    const lesson_annotation_t *annotations;
    size_t num_annotations;
} lesson_t;

/**
 * Initialize the lesson registry
 */
void lesson_init(void);

/**
 * Cleanup the lesson registry
 */
void lesson_cleanup(void);

/**
 * Register a lesson (lessons registered via constructors)
 */
void lesson_register(const lesson_t *lesson);

/**
 * Find a lesson by exploit name
 */
const lesson_t *lesson_find(const char *exploit_name);

/**
 * Get annotation for a specific step in a lesson.
 * Returns NULL if no annotation exists for this step.
 */
const lesson_annotation_t *lesson_get_annotation(const lesson_t *lesson, int step);

/**
 * Get annotation for a specific event type in a lesson.
 * Returns the first matching annotation, or NULL.
 */
const lesson_annotation_t *lesson_get_annotation_by_type(const lesson_t *lesson,
                                                          primitive_type_t type);

/**
 * Get the total number of registered lessons
 */
int lesson_count(void);

/**
 * Get a lesson by index
 */
const lesson_t *lesson_get_by_index(int index);

#endif /* LESSON_H */
