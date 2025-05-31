#ifndef __CGHOST_H__
#define __CGHOST_H__

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h> // perror
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> // exit

#ifndef CGHOST_API
#ifdef CGHOST_STATIC
#define CGHOST_API static
#else
#define CGHOST_API extern
#endif // CGHOST_STATIC
#endif // CGHOST_API

//---------------------------------------| DECLARATIONS |

// General macros
#define CAT(a, b) CAT_INNER(a, b)
#define CAT_INNER(a, b) a##b

// Allocator stack
// @allocator is actual allocator pointer such as Arena*, not CgAllocator*
typedef void *(*CgMallocFn)(void *allocator, size_t size);
typedef void *(*CgCallocFn)(void *allocator, size_t count, size_t size);
typedef void *(*CgReallocFn)(void *allocator, void *old_ptr, size_t size,
                             size_t new_size);
typedef void (*CgFreeFn)(void *allocator, void *ptr);

typedef struct CgAllocator {
  void *allocator;
  CgMallocFn malloc;
  CgCallocFn calloc;
  CgReallocFn realloc;
  CgFreeFn free;
} CgAllocator;

CGHOST_API CgAllocator std_allocator;

CGHOST_API void *std_malloc(void *a, size_t size);
CGHOST_API void *std_calloc(void *a, size_t count, size_t size);
CGHOST_API void *std_realloc(void *a, void *old_ptr, size_t size,
                             size_t new_size);
CGHOST_API void std_free(void *a, void *ptr);

#ifndef CGHOST_ALLOCATOR_STACK_SIZE
#define CGHOST_ALLOCATOR_STACK_SIZE 32
#endif

CGHOST_API CgAllocator cg_as[CGHOST_ALLOCATOR_STACK_SIZE];
CGHOST_API size_t cg_as_top;

#define CG_ALLOCATOR_PUSH(allocator) cg_as[cg_as_top++] = (allocator)
#define CG_ALLOCATOR_POP() --cg_as_top
#define CG_ALLOCATOR_CURRENT                                                   \
  (assert(cg_as_top != 0 && "Allocator stack underflow"),                      \
   assert(cg_as_top != CGHOST_ALLOCATOR_STACK_SIZE &&                          \
          "Allocator stack overflow"),                                         \
   cg_as[cg_as_top - 1])

#define CG_MALLOC CG_ALLOCATOR_CURRENT.malloc
#define CG_CALLOC CG_ALLOCATOR_CURRENT.calloc
#define CG_REALLOC CG_ALLOCATOR_CURRENT.realloc
#define CG_FREE CG_ALLOCATOR_CURRENT.free
#define CG_ALLOCATOR_INSTANCE CG_ALLOCATOR_CURRENT.allocator

// Dynamic array works on any struct with *items, count and capacity fields

#ifndef DA_GROW_FACTOR
#define DA_GROW_FACTOR 2
#endif

#ifndef DA_INIT_CAPACITY
#define DA_INIT_CAPACITY 64
#endif

// size of the static C array
#define ARR_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

#define DA_EMBED(T)                                                            \
  T *items;                                                                    \
  size_t count;                                                                \
  size_t capacity;

#define DA_STRUCT(T, name)                                                     \
  typedef struct name {                                                        \
    DA_EMBED(T)                                                                \
  } name;

#define da_from_list(T, ...)                                                   \
  da_from_list_##T((T[]){__VA_ARGS__}, sizeof((T[]){__VA_ARGS__}) / sizeof(T))

#define DA_DECL_TYPE(T, name)                                                  \
  DA_STRUCT(T, name)                                                           \
  static inline name da_from_list_##T(const T *arr, size_t count) {            \
    name result;                                                               \
    result.count = count;                                                      \
    result.capacity = count;                                                   \
    result.items = CG_MALLOC(CG_ALLOCATOR_INSTANCE, count * sizeof(T));        \
    memcpy(result.items, arr, count * sizeof(T));                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  static inline name da_clone_##T(name da) {                                   \
    return (name){                                                             \
        .items =                                                               \
            da_clone_items((da).items, sizeof((da).items[0]), (da).count),     \
        .count = da.count,                                                     \
        .capacity = da.capacity,                                               \
    };                                                                         \
  }

#ifndef DA_AUTO_SUFIX
#define DA_AUTO_SUFIX Arr
#endif

#define DA_DECL_TYPE_AUTO(T) DA_DECL_TYPE(T, CAT(T, DA_AUTO_SUFIX))

#define da_alloc_reserved(da, capacity_)                                       \
  do {                                                                         \
    size_t cap = (capacity_);                                                  \
    (da).items = CG_CALLOC(CG_ALLOCATOR_INSTANCE, cap, sizeof(*(da).items));   \
    assert(NULL != (da).items &&                                               \
           "Failed to allocate memory for dynamic array");                     \
    (da).capacity = cap;                                                       \
    (da).count = 0;                                                            \
  } while (0)

#define da_alloc(da) da_alloc_reserved((da), DA_INIT_CAPACITY)

#define da_free(da)                                                            \
  do {                                                                         \
    CG_FREE(CG_ALLOCATOR_INSTANCE, (da).items);                                \
    (da).items = NULL;                                                         \
    (da).count = 0;                                                            \
    (da).capacity = 0;                                                         \
  } while (0)

#define da_is_empty(da) ((da).count == 0)

#define da_push(da, el)                                                        \
  do {                                                                         \
    da_maybe_expand((da));                                                     \
    (da).items[(da).count++] = (el);                                           \
  } while (0)

#define da_pop(da)                                                             \
  (assert(!da_is_empty((da)) &&                                                \
          "Attempt to pop from dynamic array with size of 0"),                 \
   (da).count -= 1)

// NOTE: this macro is unsafe since it does not check if da is empty,
// so make sure that da has it least 1 item before calling this macro
#define da_back(da) (da).items[(da).count - 1]

#define da_swap_remove(da, index)                                              \
  do {                                                                         \
    (da).items[(index)] = (da).items[--(da).count];                            \
  } while (0)

#define da_append_da(da1, da2)                                                 \
  do {                                                                         \
    da_resize((da1), (da1).count + (da2).count);                               \
    for (size_t _i = 0; _i <= (da2).count; _i += 1) {                          \
      (da1).items[(da1).count++] = (da2).items[_i];                            \
    }                                                                          \
  } while (0)

#define da_resize(da, new_capacity)                                            \
  do {                                                                         \
    (da).items = CG_REALLOC(CG_ALLOCATOR_INSTANCE, (da).items,                 \
                            sizeof(*(da).items) * (da).capacity,               \
                            sizeof(*(da).items) * (new_capacity));             \
    assert(NULL != (da).items && "Failed to allocate memory for dynamic "      \
                                 "array");                                     \
    memset((da).items + (da).count, 0,                                         \
           ((new_capacity) - (da).count) * sizeof((da).items[0]));             \
    (da).capacity = (new_capacity);                                            \
  } while (0)

#define da_maybe_expand(da)                                                    \
  if ((da).count >= (da).capacity)                                             \
  da_resize((da),                                                              \
            (da).capacity ? (da).capacity *DA_GROW_FACTOR : DA_INIT_CAPACITY)

#define da_insert(da, index, el)                                               \
  do {                                                                         \
    if ((index) > (da).count) {                                                \
      fprintf(stderr, "Index %zu out of bounds for insert (size: %zu)\n",      \
              (size_t)(index), (size_t)(da).count);                            \
      exit(1);                                                                 \
    }                                                                          \
    da_maybe_expand((da));                                                     \
    for (size_t _i = (da).count; _i > (index); --_i) {                         \
      (da).items[_i] = (da).items[_i - 1];                                     \
    }                                                                          \
    (da).items[index] = (el);                                                  \
    (da).count += 1;                                                           \
  } while (0)

#define da_fprintf(da, f, el_format)                                           \
  do {                                                                         \
    for (size_t _i = 0; _i < (da).count; _i += 1) {                            \
      fprintf((f), (el_format), (da).items[_i]);                               \
    }                                                                          \
  } while (0)

#define da_fprintf_stringify(da, f, el_format, stringify)                      \
  do {                                                                         \
    for (size_t _i = 0; _i < (da).count; _i += 1) {                            \
      fprintf((f), (el_format), stringify((da).items[_i]));                    \
    }                                                                          \
  } while (0)

CGHOST_API void *da_clone_items(void *items, size_t el_size, size_t count);

// StringView
typedef struct {
  const char *begin;
  size_t length;
} StringView;

#define sv_empty ((StringView){0})
#define sv_from_cstr(str) ((StringView){.begin = str, .length = strlen(str)})
#define sv_from_cstr_slice(str, offset, len)                                   \
  ((StringView){.begin = (str) + (offset), .length = len})
#define sv_from_constant(c)                                                    \
  ((StringView){.begin = (c), .length = sizeof((c)) - 1})
#define sv_from_sb(sb) ((StringView){.begin = (sb).items, .length = (sb).count})
#define sv_from_str(str) sv_from_sb((str).h->b)

#define sv_farg "%.*s"
#define sv_expand(sv) (int)(sv).length, (sv).begin

#define sv_slice(sv, offset, len)                                              \
  (sv_from_cstr_slice((sv).begin, (offset), (len)))

#define sv_advance(sv) (++(sv).begin, --(sv).length)

CGHOST_API bool sv_equals(const StringView *lhs, const StringView *rhs);
CGHOST_API bool sv_equals_icase(const StringView *lhs, const StringView *rhs);

CGHOST_API bool sv_starts_with_cstr(const StringView *sv, const char *start);
CGHOST_API bool sv_starts_with_cstr_icase(const StringView *sv,
                                          const char *start);
CGHOST_API bool sv_ends_with_cstr(const StringView *sv, const char *end);
CGHOST_API bool sv_ends_with_cstr_icase(const StringView *sv, const char *end);

CGHOST_API bool sv_starts_with(const StringView *sv, const StringView *start);
CGHOST_API bool sv_starts_with_icase(const StringView *sv,
                                     const StringView *start);
CGHOST_API bool sv_ends_with(const StringView *sv, const StringView *end);
CGHOST_API bool sv_ends_with_icase(const StringView *sv, const StringView *end);
CGHOST_API void sv_stripl(StringView *sv);

CGHOST_API int sv_index_of(const StringView *sv, int rune);
CGHOST_API int sv_last_index_of(const StringView *sv, int rune);
CGHOST_API int sv_index_of_str(const StringView *sv, const char *str);
CGHOST_API StringView sv_split(StringView *sv, const char *delim);
CGHOST_API StringView sv_split_exclude_delim(StringView *sv, const char *delim);

// StringBuilder

typedef struct {
  char *items;
  size_t count;
  size_t capacity;
} StringBuilder;

#define sb_free(sb) da_free((sb))
#define sb_length(sb) da_count((sb))
#define sb_expand_buffer(sb, new_capacity)                                     \
  do {                                                                         \
    if ((sb).capacity < (new_capacity)) {                                      \
      da_resize((sb), (new_capacity));                                         \
    }                                                                          \
  } while (0)

#define sb_farg "%.*s"
#define sb_expand(sb) (int)(sb).count, (sb).items

CGHOST_API StringBuilder sb_create(size_t capacity);
CGHOST_API StringBuilder sb_clone(const StringBuilder *sb);
CGHOST_API void sb_append_rune(StringBuilder *sb, int rune);
CGHOST_API void sb_append_string_view(StringBuilder *sb, const StringView *sv);
CGHOST_API void sb_append_cstr(StringBuilder *sb, const char *cstr);
CGHOST_API void sb_append_sb(StringBuilder *dest, const StringBuilder *src);
#define sb_append_str(sb_ptr, str_ptr) sb_append_sb((sb_ptr), &(str_ptr)->h->b)
CGHOST_API char *sb_get_cstr(StringBuilder *sb);
CGHOST_API void sb_appendf(StringBuilder *sb, const char *fmt, ...);

// COW String

typedef struct CowStrHeader {
  StringBuilder b;
  size_t refcount;
} CowStrHeader;

typedef struct CowStr {
  CowStrHeader *h;
} Str;

#define str_is_unique(str) ((str).h->refcount == 1)
#define str_make_unique(str)                                                   \
  (str_is_unique((str)) ? (str) : str_clone_unique(&(str)))
#define str_empty str_create(0)

#define str_farg "%.*s"
#define str_expand(str) sb_expand((str).h->b)

CGHOST_API Str str_create(size_t capacity);
CGHOST_API Str str_from_sv(StringView sv);
CGHOST_API Str str_from_sb(StringBuilder sb);
CGHOST_API Str str_from_cstr(const char *cstr);
CGHOST_API Str str_clone(Str *str);
CGHOST_API Str str_clone_unique(Str *str);
CGHOST_API Str str_move(Str *str);
CGHOST_API void str_free(Str *str);

// IO and File system
CGHOST_API bool read_entire_file(const char *path, StringBuilder *sb);

CGHOST_API bool mkdirp(StringView path, mode_t mode);

// Command Line Arguments Parsing

#define shift_args(argc, argv) ((argc) -= 1, (argv) += 1)

typedef enum ClargKind {
  CLA_BOOL,
  CLA_SIZE_T,
  CLA_SSIZE_T,
  CLA_DOUBLE,
  CLA_STRING,
  CLA_LIST,
} ClargKind;

typedef struct ClargValue {
  ClargKind kind;
  union {
    bool as_bool;
    size_t as_size_t;
    ssize_t as_ssize_t;
    double as_double;
    StringView as_sv;
    struct {
      DA_EMBED(StringView)
    } as_list;
  } v;
} ClargValue;

// Clarg - command line argument
typedef enum ClargError {
  CLARG_NO_ERROR,
  CLARG_UNKNOWN_ARGUMENT,
  CLARG_NO_VALUE,
} ClargError;

typedef struct Clarg {
  // TODO: add support for short and long names (like -h, --help)
  const char *name;
  const char *description;
  ClargValue default_val;
  ClargValue value;
} Clarg;

DA_STRUCT(Clarg, Clargs)

typedef struct ClargParser {
  Clargs options;
  ClargError err;
  const char *errarg;
} ClargParser;

CGHOST_API void *clargs_add_flag(ClargParser *p, const char *name,
                                 ClargValue default_val,
                                 const char *description);
CGHOST_API void clargs_free(ClargParser *p);
CGHOST_API bool clargs_parse(ClargParser *p, int argc, char **argv);
CGHOST_API void clargs_print_options(ClargParser *p, FILE *f);
CGHOST_API void clargs_print_error(ClargParser *p, FILE *f);

// Arena
#ifndef ARENA_CHUNK_SIZE
#define ARENA_CHUNK_SIZE (1024 * 1024)
#endif

#define ARENA_ALLOC_TAG 0xCAFEBABE

#define ARENA_ALIGNMENT 8

#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#if !defined(__CGHOST_MEMORY_DEBUG) && !defined(NDEBUG) &&                     \
    !defined(CGHOST_MEMORY_DEBUG)
#define __CGHOST_MEMORY_DEBUG
#endif

#define ARENA_ALLOC_HEADER(ptr)                                                \
  (ArenaAllocHeader *)((char *)(ptr) - offsetof(ArenaAllocHeader, data))

#ifdef __CGHOST_MEMORY_DEBUG
#define ARENA_TRACE_PTR(ptr)                                                   \
  do {                                                                         \
    ArenaAllocHeader *header = ARENA_ALLOC_HEADER((ptr));                      \
    header->line = __LINE__;                                                   \
    header->file = __FILE__;                                                   \
  } while (0)
#else
#define ARENA_TRACE_PTR(ptr)
#endif // __CGHOST_MEMORY_DEBUG

typedef struct ArenaAllocHeader {
#ifdef __CGHOST_MEMORY_DEBUG
  void *owner;
  size_t size;
  const char
      *file; // defaults to NULL, can be set manually or with ARENA_TRACE_PTR
  int line;  // defaults to zero, can be set manually or with ARENA_TRACE_PTR
  uint32_t tag;

  // Padding to align `data[]` field if needed
  // NOTE: the size of _pad maybe evaluated to zero that is forbidden by ISO C,
  // in this case if your compiler does not support zero-size array, just
  // comment it out
  // TODO: figure out proper portable solution
  unsigned char _pad[sizeof(size_t) - 2 * sizeof(int)];
#endif // __CGHOST_MEMORY_DEBUG

  char data[]; // flexible array member
} ArenaAllocHeader;

typedef struct ArenaChunk {
  struct ArenaChunk *next;
  size_t used;
  size_t ref_count;
  char memory[ARENA_CHUNK_SIZE];
} ArenaChunk;

typedef struct Arena {
  ArenaChunk *chunks;
} Arena;

CGHOST_API Arena garena;

// === Declarations ===

CGHOST_API void *arena_alloc(Arena *arena, size_t size);
CGHOST_API void *arena_calloc(Arena *arena, size_t count, size_t size);
CGHOST_API void *arena_realloc(Arena *arena, void *old_ptr, size_t old_size,
                               size_t new_size);
CGHOST_API void arena_return(Arena *arena, void *ptr);
CGHOST_API void arena_free(Arena *arena);
CGHOST_API CgAllocator arena_create_allocator(Arena *arena);

// global versions
CGHOST_API void *garena_alloc(size_t size);
CGHOST_API void *garena_calloc(size_t count, size_t size);
CGHOST_API void *garena_realloc(void *old_ptr, size_t old_size,
                                size_t new_size);
CGHOST_API void garena_return(void *ptr); // soft-free
CGHOST_API void garena_free(void);

CGHOST_API CgAllocator garena_allocator;

// === Inline helpers ===
#define arena_alloc_t(arena, Type) ((Type *)arena_alloc((arena), sizeof(Type)))
#define arena_alloc_array(arena, Type, count)                                  \
  ((Type *)arena_alloc((arena), sizeof(Type) * (count)))

#define g_alloc_t(Type) ((Type *)g_alloc(sizeof(Type)))
#define g_alloc_array(Type, count) ((Type *)g_alloc(sizeof(Type) * (count)))

#endif // __CGHOST_H__

//---------------------------------------| IMPLEMENATION |
#ifdef CGHOST_IMPLEMENTATION

#include <ctype.h>
#include <errno.h>
#include <stdalign.h>
#include <stdarg.h>

// Allocator
CgAllocator cg_as[CGHOST_ALLOCATOR_STACK_SIZE];
size_t cg_as_top;

// stdallocator
CgAllocator std_allocator = {
    .allocator = NULL,
    .malloc = std_malloc,
    .calloc = std_calloc,
    .realloc = std_realloc,
    .free = std_free,
};

CGHOST_API void *std_malloc(void *a, size_t size) {
  (void)a;
  return malloc(size);
}

CGHOST_API void *std_calloc(void *a, size_t count, size_t size) {
  (void)a;
  return calloc(count, size);
}

CGHOST_API void *std_realloc(void *a, void *old_ptr, size_t size,
                             size_t new_size) {
  (void)a;
  (void)size;
  return realloc(old_ptr, new_size);
}

CGHOST_API void std_free(void *a, void *ptr) {
  (void)a;
  free(ptr);
}

// Dynamic array
CGHOST_API void *da_clone_items(void *items, size_t el_size, size_t count) {
  void *clone = CG_MALLOC(CG_ALLOCATOR_INSTANCE, count * el_size);
  memcpy(clone, items, count * el_size);
  return clone;
}

// StringView
CGHOST_API bool sv_equals(const StringView *lhs, const StringView *rhs) {
  assert(NULL != lhs);
  assert(NULL != rhs);

  return lhs->length == rhs->length &&
         0 == strncmp(lhs->begin, rhs->begin, lhs->length);
}

CGHOST_API bool sv_equals_icase(const StringView *lhs, const StringView *rhs) {
  assert(NULL != lhs);
  assert(NULL != rhs);

  return lhs->length == rhs->length &&
         0 == strncasecmp(lhs->begin, rhs->begin, lhs->length);
}

CGHOST_API bool sv_starts_with_cstr(const StringView *sv, const char *start) {
  assert(NULL != sv);

  size_t start_len = strlen(start);
  return sv->length >= start_len && 0 == strncmp(sv->begin, start, start_len);
}

CGHOST_API bool sv_starts_with_cstr_icase(const StringView *sv,
                                          const char *start) {
  size_t start_len = strlen(start);
  return sv->length >= start_len &&
         0 == strncasecmp(sv->begin, start, start_len);
}

CGHOST_API bool sv_ends_with_cstr(const StringView *sv, const char *end) {
  assert(NULL != sv);

  size_t end_len = strlen(end);
  return sv->length >= end_len &&
         0 == strncmp(sv->begin + sv->length - end_len, end, end_len);
}

CGHOST_API bool sv_ends_with_cstr_icase(const StringView *sv, const char *end) {
  size_t end_len = strlen(end);
  return sv->length >= end_len &&
         0 == strncasecmp(sv->begin + sv->length - end_len, end, end_len);
}

CGHOST_API bool sv_starts_with(const StringView *sv, const StringView *start) {
  return sv->length >= start->length &&
         0 == strncmp(sv->begin, start->begin, start->length);
}

CGHOST_API bool sv_starts_with_icase(const StringView *sv,
                                     const StringView *start) {
  return sv->length >= start->length &&
         0 == strncasecmp(sv->begin, start->begin, start->length);
}

CGHOST_API bool sv_ends_with(const StringView *sv, const StringView *end) {
  return sv->length >= end->length &&
         0 == strncmp(sv->begin + sv->length - end->length, end->begin,
                      end->length);
}

CGHOST_API bool sv_ends_with_icase(const StringView *sv,
                                   const StringView *end) {
  return sv->length >= end->length &&
         0 == strncasecmp(sv->begin + sv->length - end->length, end->begin,
                          end->length);
}

CGHOST_API void sv_stripl(StringView *sv) {
  while (sv->length > 0 && isspace(sv->begin[0]))
    sv_advance(*sv);
}

CGHOST_API int sv_index_of(const StringView *sv, int rune) {
  assert(NULL != sv);

  const char *found = strchr(sv->begin, rune);
  return found - sv->begin;
}

CGHOST_API int sv_last_index_of(const StringView *sv, int rune) {
  assert(NULL != sv);

  const char *found = strrchr(sv->begin, rune);
  return found - sv->begin;
}

CGHOST_API int sv_index_of_str(const StringView *sv, const char *str) {
  assert(NULL != sv);
  const char *substr = strstr(sv->begin, str);
  return NULL == substr ? -1 : substr - sv->begin;
}

CGHOST_API StringView sv_split(StringView *sv, const char *delim) {
  // returns StringView before the delim (not including)
  // sv becomes StringView from the delim (including) to the end of the given sv
  int index = sv_index_of_str(sv, delim);
  if (index < 0) {
    StringView result = *sv;
    sv->length = 0;
    return result;
  }

  StringView result = sv_slice(*sv, 0, index);
  *sv = sv_slice(*sv, index, sv->length - index);
  return result;
}

CGHOST_API StringView sv_split_exclude_delim(StringView *sv,
                                             const char *delim) {
  int index = sv_index_of_str(sv, delim);
  if (index < 0) {
    StringView result = *sv;
    sv->length = 0;
    return result;
  }

  StringView result = sv_slice(*sv, 0, index);
  size_t delimlen = strlen(delim);
  *sv = sv_slice(*sv, index + delimlen, sv->length - (index + delimlen));
  return result;
}

// StringBuilder
CGHOST_API StringBuilder sb_create(size_t capacity) {
  StringBuilder sb = {0};
  da_alloc_reserved(sb, capacity ? capacity : DA_INIT_CAPACITY);
  return sb;
}

CGHOST_API StringBuilder sb_clone(const StringBuilder *sb) {
  StringBuilder clone = sb_create(sb->count);
  memcpy(clone.items, sb->items, sb->count);
  return clone;
}

CGHOST_API void sb_append_rune(StringBuilder *sb, int rune) {
  assert(rune >= -128 && rune <= 127);
  da_push(*sb, (char)rune);
}

CGHOST_API void sb_append_string_view(StringBuilder *sb, const StringView *sv) {
  for (size_t i = 0; i < sv->length; ++i) {
    sb_append_rune(sb, sv->begin[i]);
  }
}

CGHOST_API void sb_append_cstr(StringBuilder *sb, const char *cstr) {
  StringView sv = sv_from_cstr(cstr);
  sb_append_string_view(sb, &sv);
}

CGHOST_API void sb_append_sb(StringBuilder *dest, const StringBuilder *src) {
  size_t new_capacity = dest->count + src->count;
  if (dest->capacity < new_capacity) {
    sb_expand_buffer(*dest, new_capacity);
    dest->capacity = new_capacity;
  }
  memcpy(dest->items + dest->count, src->items, src->count);
  dest->count = new_capacity;
}

CGHOST_API char *sb_get_cstr(StringBuilder *sb) {
  if ('\0' != sb->items[sb->count])
    da_push(*sb, '\0');

  return sb->items;
}

CGHOST_API void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
  assert(NULL != sb);
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  sb_expand_buffer(*sb, sb->count + n + 1);
  va_start(args, fmt);
  vsnprintf(sb->items + sb->count, n + 1, fmt, args);
  va_end(args);
  sb->count += n;
}

// IO
CGHOST_API bool read_entire_file(const char *path, StringBuilder *sb) {
  bool result = true;

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    result = false;
    goto defer;
  }

  if (fseek(f, 0, SEEK_END) < 0) {
    result = false;
    goto defer;
  }

  long m = ftell(f);
  if (m < 0) {
    result = false;
    goto defer;
  }

  if (fseek(f, 0, SEEK_SET) < 0) {
    result = false;
    goto defer;
  }

  size_t new_count = sb->count + m;
  sb_expand_buffer(*sb, new_count);

  fread(sb->items + sb->count, m, 1, f);
  if (ferror(f)) {
    result = false;
    goto defer;
  }
  sb->count = new_count;

defer:
  if (!result)
    fprintf(stderr, "Could not read file %s: %s\n", path, strerror(errno));
  if (f)
    fclose(f);
  return result;
}

CGHOST_API bool mkdirp(StringView path, mode_t mode) {
  StringBuilder sb = sb_create(path.length + 1);

  while (path.length > 0) {
    StringView currdir = sv_split_exclude_delim(&path, "/");
    if (currdir.length == 0)
      continue;

    sb_append_string_view(&sb, &currdir);
    sb_append_rune(&sb, '\0');

    if (mkdir(sb.items, mode) != 0 && errno != EEXIST) {
      perror(sb.items);
      sb_free(sb);
      return false;
    }

    sb.count -= 1;
    sb_append_rune(&sb, '/');
  }

  sb_free(sb);
  return true;
}

// COW String

CGHOST_API Str str_create(size_t capacity) {
  CowStrHeader *h = CG_MALLOC(CG_ALLOCATOR_INSTANCE, sizeof(CowStrHeader));
  assert(NULL != h);
  *h = (CowStrHeader){
      .b = sb_create(capacity),
      .refcount = 1,
  };
  return (Str){.h = h};
}

CGHOST_API Str str_from_sv(StringView sv) {
  Str str = str_create(sv.length);
  memcpy(str.h->b.items, sv.begin, sv.length);
  str.h->b.count = sv.length;
  return str;
}

CGHOST_API Str str_from_cstr(const char *cstr) {
  return str_from_sv(sv_from_cstr(cstr));
}

CGHOST_API Str str_from_sb(StringBuilder sb) {
  bool terminated = sb.count > 0 && sb.items[sb.count - 1] == '\0';
  Str str = str_create(sb.count + !terminated);
  memcpy(str.h->b.items, sb.items, sb.count);
  if (!terminated) {
    sb_append_rune(&str.h->b, '\0');
  }
  return str;
}

CGHOST_API Str str_clone_unique(Str *str) {
  return str_from_sv(sv_from_str(*str));
}

CGHOST_API Str str_move(Str *str) {
  if (str_is_unique(*str)) {
    return *str;
  } else {
    Str clone = str_clone_unique(str);
    str_free(str);
    return clone;
  }
}

CGHOST_API Str str_clone(Str *str) {
  str->h->refcount += 1;
  return (Str){.h = str->h};
}

CGHOST_API void str_free(Str *str) {
  if (NULL == str || NULL == str->h)
    return;
  str->h->refcount -= 1;
  if (str->h->refcount <= 0) {
    sb_free(str->h->b);
    CG_FREE(CG_ALLOCATOR_INSTANCE, str->h);
  }
  str->h = NULL;
}

// Clargs
CGHOST_API void *clargs_add_flag(ClargParser *p, const char *name,
                                 ClargValue default_val,
                                 const char *description) {
  Clarg clarg = {
      .name = name,
      .value = default_val,
      .default_val = default_val,
      .description = description,
  };

  da_push(p->options, clarg);
  return &da_back(p->options).value.v;
}

CGHOST_API void clargs_free(ClargParser *p) { da_free(p->options); }

CGHOST_API bool clargs_parse(ClargParser *p, int argc, char **argv) {
  p->err = CLARG_NO_ERROR;
  p->errarg = NULL;

  for (int j = 0; j < argc; j += 1) {
    bool matched = false;
    for (int i = 0; i < (int)p->options.count; i += 1) {
      Clarg *clarg = &p->options.items[i];
      if (0 == strcmp(argv[j], clarg->name)) {
        switch (clarg->value.kind) {
        case CLA_BOOL: {
          matched = true;
          clarg->value.v.as_bool = true;
        } break;

        case CLA_SIZE_T: {
          matched = true;
          j += 1;
          if (j == argc) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
          clarg->value.v.as_size_t = strtoull(argv[j], NULL, 10);
        } break;

        case CLA_SSIZE_T: {
          matched = true;
          j += 1;
          if (j == argc) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
          clarg->value.v.as_ssize_t = strtoll(argv[j], NULL, 10);
        } break;

        case CLA_DOUBLE: {
          matched = true;
          j += 1;
          if (j == argc) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
          clarg->value.v.as_double = strtod(argv[j], NULL);
        } break;

        case CLA_STRING: {
          matched = true;
          j += 1;
          if (j == argc) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
          clarg->value.v.as_sv = sv_from_cstr(argv[j]);
        } break;
        case CLA_LIST: {
          matched = true;
          j += 1;
          if (j == argc) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
          while (j < argc) {
            bool found_next_option = false;
            for (size_t k = 0; k < p->options.count; k += 1) {
              if (0 == strcmp(p->options.items[k].name, argv[j])) {
                found_next_option = true;
                break;
              }
            }

            if (found_next_option) {
              j -= 1;
              break;
            }

            da_push(clarg->value.v.as_list, sv_from_cstr(argv[j]));
            j += 1;
          }

          if (clarg->value.v.as_list.count == 0) {
            p->err = CLARG_NO_VALUE;
            p->errarg = argv[j - 1];
            return false;
          }
        } break;
        }
      }

      if (matched)
        break;
    }

    if (!matched) {
      p->err = CLARG_UNKNOWN_ARGUMENT;
      p->errarg = argv[j];
      return false;
    }
  }

  return true;
}

CGHOST_API void clargs_print_options(ClargParser *p, FILE *f) {
  for (size_t i = 0; i < p->options.count; i += 1) {
    Clarg *clarg = &p->options.items[i];

    fprintf(f, "    %s\n", clarg->name);
    fprintf(f, "        %s\n", clarg->description);
    switch (clarg->value.kind) {
    case CLA_BOOL:
      fprintf(f, "        Default: %s\n",
              clarg->default_val.v.as_bool ? "true" : "false");
      break;
    case CLA_SIZE_T:
      fprintf(f, "        Default: %zu\n", clarg->default_val.v.as_size_t);
      break;
    case CLA_SSIZE_T:
      fprintf(f, "        Default: %zd\n", clarg->default_val.v.as_ssize_t);
      break;
    case CLA_DOUBLE:
      fprintf(f, "        Default: %f\n", clarg->default_val.v.as_double);
      break;
    case CLA_STRING:
      fprintf(f, "        Default: " sv_farg "\n",
              sv_expand(clarg->default_val.v.as_sv));
      break;
    case CLA_LIST: {
      fprintf(f, "        Default: ");
      for (size_t j = 0; j < clarg->default_val.v.as_list.count; j += 1) {
        fprintf(f, sv_farg, sv_expand(clarg->default_val.v.as_list.items[j]));
      }
      fprintf(f, "\n");
    } break;
    }
  }
}

CGHOST_API void clargs_print_error(ClargParser *p, FILE *f) {
  fprintf(f, "Error parsing command line arguments: ");
  switch (p->err) {
  case CLARG_NO_ERROR:
    fprintf(f, "success.\n");
    break;
  case CLARG_UNKNOWN_ARGUMENT:
    fprintf(f, "unknown argument `%s`\n", p->errarg);
    break;
  case CLARG_NO_VALUE:
    fprintf(f, "no value provided for argument `%s`\n", p->errarg);
    break;
  }
}

// Arena
Arena garena = {0};

static ArenaChunk *arena_chunk_create(void) {
  return (ArenaChunk *)calloc(
      1, sizeof(ArenaChunk)); // calloc zeroes memory and all fields
}

CGHOST_API void *arena_alloc(Arena *arena, size_t size) {
  if (size > ARENA_CHUNK_SIZE)
    return NULL;

  size_t total_size =
      ALIGN_UP(sizeof(ArenaAllocHeader) + size, ARENA_ALIGNMENT);
  ArenaChunk *chunk = arena->chunks;

  while (chunk && (ARENA_CHUNK_SIZE - ALIGN_UP(chunk->used, ARENA_ALIGNMENT)) <
                      total_size) {
    chunk = chunk->next;
  }

  if (!chunk) {
    chunk = arena_chunk_create();
    if (!chunk)
      return NULL;
    chunk->next = arena->chunks;
    arena->chunks = chunk;
  }

  size_t aligned_offset = ALIGN_UP(chunk->used, ARENA_ALIGNMENT);
  if (aligned_offset + total_size > ARENA_CHUNK_SIZE)
    return NULL; // Should not happen

  ArenaAllocHeader *header =
      (ArenaAllocHeader *)(chunk->memory + aligned_offset);
  chunk->used = aligned_offset + total_size;
  chunk->ref_count++;

#ifdef __CGHOST_MEMORY_DEBUG
  header->owner = chunk;
  header->size = size;
  header->file = NULL;
  header->line = 0;
  header->tag = ARENA_ALLOC_TAG;
#endif

  return header->data;
}

CGHOST_API void *arena_calloc(Arena *arena, size_t count, size_t size) {
  void *ptr = arena_alloc(arena, count * size);
  if (ptr) {
    memset(ptr, 0, count * size);
  }
  return ptr;
}

CGHOST_API void arena_return(Arena *arena, void *ptr) {
  if (!ptr)
    return;

  for (ArenaChunk **link = &arena->chunks; *link;) {
    ArenaChunk *chunk = *link;
    if ((char *)ptr >= chunk->memory &&
        (char *)ptr < chunk->memory + ARENA_CHUNK_SIZE) {

#ifdef __CGHOST_MEMORY_DEBUG
      ArenaAllocHeader *header = ARENA_ALLOC_HEADER(ptr);
      if (header->owner != chunk) {
        fprintf(stderr,
                "[Error] Arena pointer does not match expected chunk\n");
        abort();
      }

      if (((uintptr_t)ptr - (uintptr_t)chunk->memory) % ARENA_ALIGNMENT != 0) {
        fprintf(stderr, "[Warning] Pointer not aligned to allocation unit\n");
      }
#endif

      if (chunk->ref_count > 0) {
        chunk->ref_count--;
        if (chunk->ref_count == 0) {
          // reuse memory instead of freeing it
          chunk->used = 0;
          return;
        }
      }
      return;
    } else {
      link = &(*link)->next;
    }
  }
}

CGHOST_API void *arena_realloc(Arena *arena, void *old_ptr, size_t old_size,
                               size_t new_size) {
  if (!old_ptr)
    return arena_alloc(arena, new_size);
  if (new_size == 0) {
    arena_return(arena, old_ptr);
    return NULL;
  }

  void *new_ptr = arena_alloc(arena, new_size);
  if (!new_ptr)
    return NULL;

  size_t copy_size = old_size < new_size ? old_size : new_size;
  memcpy(new_ptr, old_ptr, copy_size);

  arena_return(arena, old_ptr);
  return new_ptr;
}

CGHOST_API void arena_free(Arena *arena) {
  ArenaChunk *chunk = arena->chunks;
  while (chunk) {
    ArenaChunk *next = chunk->next;
    free(chunk);
    chunk = next;
  }
  arena->chunks = NULL;
}

CGHOST_API CgAllocator arena_create_allocator(Arena *arena) {
  return (CgAllocator){
      .allocator = arena,
      .malloc = (CgMallocFn)arena_alloc,
      .calloc = (CgCallocFn)arena_calloc,
      .realloc = (CgReallocFn)arena_realloc,
      .free = (CgFreeFn)arena_return,
  };
}

// === Global arena wrappers ===

CgAllocator garena_allocator = {
    .allocator = &garena,
    .malloc = (CgMallocFn)arena_alloc,
    .calloc = (CgCallocFn)arena_calloc,
    .realloc = (CgReallocFn)arena_realloc,
    .free = (CgFreeFn)arena_return,
};

CGHOST_API void *garena_alloc(size_t size) {
  return arena_alloc(&garena, size);
}

CGHOST_API void *garena_calloc(size_t count, size_t size) {
  return arena_calloc(&garena, count, size);
}

CGHOST_API void *garena_realloc(void *old_ptr, size_t old_size,
                                size_t new_size) {
  return arena_realloc(&garena, old_ptr, old_size, new_size);
}

CGHOST_API void garena_return(void *ptr) { arena_return(&garena, ptr); }

CGHOST_API void garena_free(void) { arena_free(&garena); }

#endif // CGHOST_IMPLEMENTATION
