#include "lesson.h"

/* ===== Use-After-Free Lesson ===== */

static const lesson_annotation_t uaf_annotations[] = {
    {
        .step_number = 1, .trigger_type = PRIM_NONE,
        .title = "Setup Phase",
        .explanation = "A buffer is allocated on the heap. This is the 'victim' "
                       "object that will be used after being freed.",
        .mitigation = "N/A - legitimate allocation.",
        .related_steps = {5, 7, -1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = 5, .trigger_type = PRIM_NONE,
        .title = "Free Without Nulling Pointer",
        .explanation = "The buffer is freed but the pointer variable still holds "
                       "the old address. This creates a 'dangling pointer' - the "
                       "pointer looks valid but the memory it references is no "
                       "longer owned by the program.",
        .mitigation = "Always set pointers to NULL after freeing: free(ptr); ptr = NULL; "
                       "Use smart pointers in C++ or ownership models in Rust.",
        .related_steps = {1, 7, -1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_UAF,
        .title = "Use-After-Free Triggered!",
        .explanation = "The program reads or writes through the dangling pointer. "
                       "Since the memory was freed, it may have been reallocated "
                       "for a different purpose. The attacker controls what data "
                       "is now at that address, leading to arbitrary read/write.",
        .mitigation = "Use memory sanitizers (ASAN) during development. Consider "
                       "using a garbage-collected language or Rust's borrow checker.",
        .related_steps = {5, -1},
        .level = LEVEL_BASIC
    },
};

static const lesson_t uaf_lesson = {
    .exploit_name = "Use-After-Free",
    .overview = "Use-After-Free (UAF) occurs when a program continues to use "
                "a pointer after the memory it points to has been freed. This "
                "is one of the most common and dangerous memory safety bugs, "
                "frequently exploited in browsers, kernels, and system software.",
    .annotations = uaf_annotations,
    .num_annotations = sizeof(uaf_annotations) / sizeof(uaf_annotations[0]),
};

/* ===== Double-Free Lesson ===== */

static const lesson_annotation_t double_free_annotations[] = {
    {
        .step_number = 3, .trigger_type = PRIM_NONE,
        .title = "Initial Allocation",
        .explanation = "A chunk is allocated from the heap. The allocator tracks "
                       "this chunk in its internal metadata (size, flags, etc.).",
        .mitigation = "N/A - legitimate allocation.",
        .related_steps = {7, 11, -1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = 7, .trigger_type = PRIM_NONE,
        .title = "First Free (Legitimate)",
        .explanation = "The chunk is freed and returned to the allocator's free "
                       "list (tcache or fastbin in glibc). The allocator marks "
                       "the chunk as free in its metadata.",
        .mitigation = "N/A - legitimate free.",
        .related_steps = {3, 11, -1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_DOUBLE_FREE,
        .title = "Double-Free Detected!",
        .explanation = "The same chunk is freed a second time. This corrupts the "
                       "allocator's free list, causing it to contain a cycle. "
                       "Subsequent allocations can return the same chunk twice, "
                       "giving the attacker overlapping objects for type confusion.",
        .mitigation = "Set pointers to NULL after free. Use ASAN or mtrace to "
                       "detect double-frees. Modern allocators (e.g., tcache in "
                       "glibc 2.29+) have double-free checks.",
        .related_steps = {7, -1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_OVERFLOW,
        .title = "Heap Metadata Overwrite",
        .explanation = "With overlapping allocations from the corrupted free list, "
                       "the attacker can overwrite heap metadata or adjacent objects "
                       "to gain code execution.",
        .mitigation = "Use heap hardening: ASLR, guard pages, randomized allocation.",
        .related_steps = {-1},
        .level = LEVEL_INTERMEDIATE
    },
};

static const lesson_t double_free_lesson = {
    .exploit_name = "Double-Free",
    .overview = "A double-free vulnerability occurs when free() is called twice "
                "on the same pointer. This corrupts the heap allocator's internal "
                "free list, potentially allowing an attacker to control future "
                "allocations and achieve arbitrary write primitives.",
    .annotations = double_free_annotations,
    .num_annotations = sizeof(double_free_annotations) / sizeof(double_free_annotations[0]),
};

/* ===== Buffer Overflow Lesson ===== */

static const lesson_annotation_t overflow_annotations[] = {
    {
        .step_number = 1, .trigger_type = PRIM_NONE,
        .title = "Buffer Allocation",
        .explanation = "A fixed-size buffer is allocated. The size is determined "
                       "at allocation time, but there's no runtime check to "
                       "prevent writing beyond this size.",
        .mitigation = "Use bounded allocation with size tracking.",
        .related_steps = {-1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_OVERFLOW,
        .title = "Buffer Overflow!",
        .explanation = "Data is written past the end of the allocated buffer. "
                       "This overwrites adjacent memory, which may include heap "
                       "metadata, other objects, return addresses (on stack), "
                       "or function pointers. This is the classic exploitation "
                       "primitive for gaining code execution.",
        .mitigation = "Always check buffer bounds before writing. Use strncpy "
                       "instead of strcpy, snprintf instead of sprintf. Enable "
                       "stack canaries (-fstack-protector) and FORTIFY_SOURCE.",
        .related_steps = {-1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_WRITE,
        .title = "Controlled Write",
        .explanation = "After the overflow corrupts adjacent data, the attacker "
                       "can craft specific values to redirect control flow or "
                       "modify security-critical data structures.",
        .mitigation = "Use W^X (Write XOR Execute) memory protections. Enable "
                       "ASLR to randomize memory layout.",
        .related_steps = {-1},
        .level = LEVEL_INTERMEDIATE
    },
};

static const lesson_t overflow_lesson = {
    .exploit_name = "Buffer Overflow",
    .overview = "Buffer overflow is one of the oldest and most well-known "
                "vulnerability classes. It occurs when a program writes data "
                "beyond the bounds of an allocated buffer, corrupting adjacent "
                "memory. Stack overflows can overwrite return addresses; heap "
                "overflows can corrupt allocator metadata or adjacent objects.",
    .annotations = overflow_annotations,
    .num_annotations = sizeof(overflow_annotations) / sizeof(overflow_annotations[0]),
};

/* ===== Integer Overflow Lesson ===== */

static const lesson_annotation_t integer_overflow_annotations[] = {
    {
        .step_number = -1, .trigger_type = PRIM_INTEGER_OVERFLOW,
        .title = "Integer Overflow!",
        .explanation = "An arithmetic operation produces a result that exceeds "
                       "the maximum value for its integer type, causing it to "
                       "wrap around. A large value becomes small (or negative), "
                       "which can lead to undersized allocations, incorrect "
                       "bounds checks, or other logic errors.",
        .mitigation = "Use safe integer arithmetic libraries. Check for overflow "
                       "before the operation: if (a > SIZE_MAX - b) error(); "
                       "Use compiler builtins like __builtin_add_overflow().",
        .related_steps = {-1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_ALLOC,
        .title = "Undersized Allocation",
        .explanation = "After the integer overflow, the computed size is much "
                       "smaller than intended. The program allocates a small "
                       "buffer but believes it's large, leading to a subsequent "
                       "heap buffer overflow when data is copied in.",
        .mitigation = "Validate sizes before allocation. Use calloc(nmemb, size) "
                       "which checks for multiplication overflow internally.",
        .related_steps = {-1},
        .level = LEVEL_INTERMEDIATE
    },
};

static const lesson_t integer_overflow_lesson = {
    .exploit_name = "Integer Overflow",
    .overview = "Integer overflow occurs when arithmetic on fixed-width integers "
                "produces a result outside the representable range. The value "
                "wraps around silently in C, often turning a large number into "
                "a small one. This commonly leads to undersized heap allocations "
                "followed by buffer overflows.",
    .annotations = integer_overflow_annotations,
    .num_annotations = sizeof(integer_overflow_annotations) / sizeof(integer_overflow_annotations[0]),
};

/* ===== Format String Lesson ===== */

static const lesson_annotation_t format_string_annotations[] = {
    {
        .step_number = -1, .trigger_type = PRIM_FORMAT_STRING,
        .title = "Format String Vulnerability!",
        .explanation = "User-controlled input is passed directly as a format "
                       "string to printf/sprintf. The attacker can use format "
                       "specifiers like %x to leak stack data, %s to read "
                       "arbitrary memory, and %n to write to arbitrary addresses.",
        .mitigation = "Never pass user input as a format string. Always use "
                       "printf(\"%s\", user_input) instead of printf(user_input). "
                       "Enable -Wformat-security compiler warnings.",
        .related_steps = {-1},
        .level = LEVEL_BASIC
    },
    {
        .step_number = -1, .trigger_type = PRIM_READ,
        .title = "Information Leak",
        .explanation = "Using %x or %p format specifiers, the attacker reads "
                       "values from the stack. These can reveal ASLR base "
                       "addresses, stack canary values, or other secrets needed "
                       "to craft a full exploit chain.",
        .mitigation = "Enable ASLR and PIE. Use stack canaries. Minimize "
                       "information leakage by sanitizing error messages.",
        .related_steps = {-1},
        .level = LEVEL_INTERMEDIATE
    },
};

static const lesson_t format_string_lesson = {
    .exploit_name = "Format String",
    .overview = "Format string vulnerabilities occur when user-controlled data "
                "is used as the format argument to printf-family functions. "
                "This gives attackers the ability to read from and write to "
                "arbitrary memory locations, enabling full exploitation from "
                "a single bug.",
    .annotations = format_string_annotations,
    .num_annotations = sizeof(format_string_annotations) / sizeof(format_string_annotations[0]),
};

/* ===== Auto-registration ===== */

__attribute__((constructor))
static void register_builtin_lessons(void) {
    lesson_register(&uaf_lesson);
    lesson_register(&double_free_lesson);
    lesson_register(&overflow_lesson);
    lesson_register(&integer_overflow_lesson);
    lesson_register(&format_string_lesson);
}
