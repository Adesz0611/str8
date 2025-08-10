#ifndef STR8_H
#define STR8_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

typedef uint8_t  U8;
typedef uint16_t U16;
typedef uint32_t U32;
typedef uint64_t U64;

typedef int8_t  S8;
typedef int16_t S16;
typedef int32_t S32;
typedef int64_t S64;

typedef float  F32;
typedef double F64;

typedef uint8_t  B8;
typedef uint32_t B32;

#ifdef _MSC_VER
#define force_inline __forceinline
#elif defined (__GNUC__)
#define force_inline inline __attribute__((always_inline))
#endif

#ifdef __unix__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    U8 *buffer;
    U64 offset;
    U64 cap;
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *head;
    ArenaBlock *current;
    U64 initial_cap;
} Arena;

#define KB(x) ((U64)(x) * 1024ULL)
#define MB(x) (KB(x) * 1024ULL)
#define GB(x) (MB(x) * 1024ULL)

typedef struct Str8 {
    U8 *buffer;
    U64 len;
} Str8;

#define Str8_Fmt "%.*s"
#define Str8_Arg(s) (int)(s).len, (s).buffer

force_inline Str8 Str8_From_Zstr(U8 *txt, U32 len) { Str8 res = { (U8*)txt, len }; return res; }
#define Str8_Lit(s) Str8_From_Zstr((U8 *)s, sizeof(s) - 1)

typedef struct Str8Array {
    Str8 *elems;
    U64   len;
} Str8Array;

typedef struct Str8File {
    U8 *buffer;
    U64 size;
    U64 offset;
} Str8File;

ArenaBlock *ArenaBlock_Create(U64 cap);
B32 Arena_Create(Arena *arena, U64 cap);
void *Arena_Alloc(Arena *arena, U64 size);
void Arena_Delete(Arena *arena);

Str8 Str8_Copy(Arena *arena, Str8 s);
Str8 Str8_Append(Arena *arena, Str8 lhs, Str8 rhs);
B32 Str8_Equals(Str8 lhs, Str8 rhs);
B32 Str8_StartsWith(Str8 s, Str8 pattern);
B32 Str8_EndsWith(Str8 s, Str8 pattern);
Str8 Str8_Center(Arena *arena, Str8 s, U32 len, U8 c);
Str8 Str8_FromCStr(char *s);
U8 *Str8_ToCStr(Arena *arena, Str8 s);
Str8 Str8_Chomp(Str8 s);

// WARNING: These functions does not check overflowing
S32 Str8_ToS32(Str8 s);
U32 Str8_ToU32(Str8 s);
S64 Str8_ToS64(Str8 s);
U64 Str8_ToU64(Str8 s);
F32 Str8_ToF32(Str8 s);
F64 Str8_ToF64(Str8 s);

Str8Array Str8_SplitWhitespace(Arena *arena, Str8 s);
Str8Array Str8_SplitChar(Arena *arena, Str8 s, U8 c);
Str8 Str8_JoinChar(Arena *arena, Str8Array arr, U8 sep);
Str8 Str8_JoinStr8(Arena *arena, Str8Array arr, Str8 sep);

B32 Str8File_Load(Str8File *file, char *path);
Str8 Str8File_ReadLine(Str8File *file);
void Str8File_Close(Str8File *file);

static force_inline Str8 Str8_CenterSpace(Arena *arena, Str8 s, U32 len) {
    return Str8_Center(arena, s, len, ' ');
}

static force_inline B32 is_whitespace(U8 c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

static force_inline Str8 Str8_Substr(Str8 s, U64 offset, U64 length) {
    Str8 res;

    res.buffer = s.buffer + offset;
    res.len = length;

    return res;
}

// Cheat for operator overloading in C
#define _ARG3(_1,_2,_3,N,...) N
#define COUNT_ARGS(...) _ARG3(__VA_ARGS__, 3, 2)

#define CAT(a, b) CAT_IMPL(a, b)
#define CAT_IMPL(a, b) a##b

// Operator overloading for Str8_Split
#define Str8_Split(...) _GenericSplit(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
#define _GenericSplit(N, ...) CAT(_GenericSplit_, N)(__VA_ARGS__)
#define _GenericSplit_2(arena, s) Str8_SplitWhitespace((arena), (s))
#define _GenericSplit_3(arena, s, delim) Str8_SplitChar((arena), (s), (delim))

#define Str8_Join(arena, arr, sep) _Generic((sep), \
                                       char: Str8_JoinChar, \
                                       U8: Str8_JoinChar, \
                                       default: Str8_JoinChar, \
                                       Str8: Str8_JoinStr8 \
                                   )(arena, arr, sep)
                        

#ifdef STR8_IMPLEMENTATION

ArenaBlock *ArenaBlock_Create(U64 cap) {
    ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock));
    if (!block) return NULL;

    block->buffer = (U8 *)malloc(cap);
    if (!block->buffer) {
        free(block);
        return NULL;
    }

    uintptr_t start = (uintptr_t)block->buffer;
    uintptr_t aligned_start = (start + 7) & ~(uintptr_t)7;
    U64 padding = aligned_start - start;

    block->offset = padding;
    block->cap = cap;
    block->next = NULL;
    return block;
}

B32 Arena_Create(Arena *arena, U64 cap) {
    ArenaBlock *block = ArenaBlock_Create(cap);
    if (!block) {
        return false;
    }

    arena->head = block;
    arena->current = block;
    arena->initial_cap = cap;

    return true;
}

void *Arena_Alloc(Arena *arena, U64 size) {
    ArenaBlock *block = arena->current;

    uintptr_t raw_ptr = (uintptr_t)(block->buffer + block->offset);
    uintptr_t aligned_ptr = (raw_ptr + 7) & ~(uintptr_t)7;
    U64 padding = aligned_ptr - raw_ptr;

    if (block->offset + padding + size <= block->cap) {
        block->offset += padding + size;
        return (void *)aligned_ptr;
    }

    U64 new_cap = block->cap * 2;
    if (new_cap < size + 64) new_cap = size + 64;

    ArenaBlock *new_block = ArenaBlock_Create(new_cap);
    if (!new_block) {
        return NULL;
    }

    block->next = new_block;
    arena->current = new_block;

    uintptr_t new_raw_ptr = (uintptr_t)(new_block->buffer + new_block->offset);
    uintptr_t new_aligned_ptr = (new_raw_ptr + 7) & ~(uintptr_t)7;
    U64 new_padding = new_aligned_ptr - new_raw_ptr;

    new_block->offset += new_padding + size;
    return (void *)new_aligned_ptr;
}

void Arena_Delete(Arena *arena) {
    ArenaBlock *block = arena->head;
    while (block) {
        ArenaBlock *next = block->next;
        free(block->buffer);
        free(block);
        block = next;
    }
    arena->head = NULL;
    arena->current = NULL;
}

Str8 Str8_Copy(Arena *arena, Str8 s) {
    Str8 res;

    res.len = s.len;
    res.buffer = (U8 *)Arena_Alloc(arena, s.len);

    memcpy(res.buffer, s.buffer, s.len);

    return res;
}

Str8 Str8_Append(Arena *arena, Str8 lhs, Str8 rhs) {
    Str8 res;

    res.len = lhs.len + rhs.len;
    res.buffer = (U8 *)Arena_Alloc(arena, lhs.len + rhs.len);

    memcpy(res.buffer, lhs.buffer, lhs.len);
    memcpy(res.buffer + lhs.len, rhs.buffer, rhs.len);

    return res;
}

B32 Str8_Equals(Str8 lhs, Str8 rhs) {
    if (lhs.len != rhs.len)
        return false;

    for (U64 i = 0; i < lhs.len; ++i)
        if (lhs.buffer[i] != rhs.buffer[i])
            return false;

    return true;
}

B32 Str8_StartsWith(Str8 s, Str8 pattern) {
    if (s.len < pattern.len)
        return false;

    for (U64 i = 0; i < pattern.len; ++i)
        if (s.buffer[i] != pattern.buffer[i])
            return false;

    return true;

}

B32 Str8_EndsWith(Str8 s, Str8 pattern) {
    if (s.len < pattern.len)
        return false;

    U64 offset = s.len - pattern.len;

    for (U64 i = 0; i < pattern.len; ++i) {
        if (s.buffer[offset + i] != pattern.buffer[i])
            return false;
    }

    return true;
}

/*
 * arena: memory arena
 * s: string to be centered
 * len: length of the returned string
 * c: character to fill the missing space
 */
Str8 Str8_Center(Arena *arena, Str8 s, U32 len, U8 c) {
    Str8 res;

    if (len < s.len)
        return s;

    res.len = len;
    res.buffer = (U8 *)Arena_Alloc(arena, len);

    size_t begin_str = (len - s.len) / 2;
    size_t end_str   = begin_str + s.len;

    memset(res.buffer, c, begin_str);
    memcpy(res.buffer + begin_str, s.buffer, s.len);
    memset(res.buffer + end_str, c, len - end_str);

    return res;
}

Str8 Str8_FromCStr(char *s) {
    Str8 res;

    res.len = strlen(s);
    res.buffer = (U8 *)s;

    return res;
}

U8 *Str8_ToCStr(Arena *arena, Str8 s) {
    U8 *res;

    res = (U8 *)Arena_Alloc(arena, s.len + 1);
    memcpy(res, s.buffer, s.len);
    res[s.len] = '\0';

    return res;
}

Str8 Str8_Chomp(Str8 s) {
    if (s.len >= 2 && s.buffer[s.len - 2] == '\r' && s.buffer[s.len - 1] == '\n') {
        s.len -= 2;
    } else if (s.len >= 1 && (s.buffer[s.len - 1] == '\n' || s.buffer[s.len - 1] == '\r')) {
        s.len -= 1;
    }
    return s;
}

S32 Str8_ToS32(Str8 s) {
    S32 n=0, sign=1;

    while (!isdigit(*s.buffer) && *s.buffer != '+' && *s.buffer != '-' && s.len) {
        --s.len;
        ++s.buffer;
    }

    if (s.len > 0)
        switch(s.buffer[0]) {
            case '-':   sign=-1;
            case '+':   --s.len; ++s.buffer; break;
        }

    while (s.len-- && isdigit(*s.buffer))
        n = n*10 + *s.buffer++ - '0';

    return n*sign;
}

U32 Str8_ToU32(Str8 s) {
    U32 n=0;

    while (!isdigit(*s.buffer) && s.len) {
        --s.len;
        ++s.buffer;
    }

    while (s.len-- && isdigit(*s.buffer))
        n = n*10 + *s.buffer++ - '0';

    return n;
}

S64 Str8_ToS64(Str8 s) {
    S64 n=0, sign=1;

    while (!isdigit(*s.buffer) && *s.buffer != '+' && *s.buffer != '-' && s.len) {
        --s.len;
        ++s.buffer;
    }

    if (s.len > 0)
        switch(s.buffer[0]) {
            case '-':   sign=-1;
            case '+':   --s.len; ++s.buffer; break;
        }

    while (s.len-- && isdigit(*s.buffer))
        n = n*10 + *s.buffer++ - '0';

    return n*sign;
}

U64 Str8_ToU64(Str8 s) {
    U64 n=0;

    while (!isdigit(*s.buffer) && s.len) {
        --s.len;
        ++s.buffer;
    }

    while (s.len-- && isdigit(*s.buffer))
        n = n*10 + *s.buffer++ - '0';

    return n;
}

F32 Str8_ToF32(Str8 s) {
    if (!s.buffer || s.len == 0)
        return 0.0f;

    F32 result = 0.0;
    F32 fraction = 0.0;
    S32 exponent = 0;
    S32 sign = 1;
    F32 frac_divisor = 1;
    U64 i = 0;

    if (i < s.len && (s.buffer[i] == '-' || s.buffer[i] == '+')) {
        if (s.buffer[i] == '-') {
            sign = -1;
        }
        i++;
    }

    while (i < s.len && isdigit(s.buffer[i])) {
        result = result * 10.0 + (s.buffer[i] - '0');
        i++;
    }

    if (i < s.len && s.buffer[i] == '.') {
        i++;
        while (i < s.len && isdigit(s.buffer[i])) {
            fraction = fraction * 10.0 + (s.buffer[i] - '0');
            frac_divisor *= 10.0;
            i++;
        }
    }

    result += fraction / frac_divisor;

    if (i < s.len && (s.buffer[i] == 'e' || s.buffer[i] == 'E')) {
        i++;
        S32 exp_sign = 1;
        S32 exp_value = 0;

        if (i < s.len && (s.buffer[i] == '-' || s.buffer[i] == '+')) {
            if (s.buffer[i] == '-') {
                exp_sign = -1;
            }
            i++;
        }

        while (i < s.len && isdigit(s.buffer[i])) {
            exp_value = exp_value * 10 + (s.buffer[i] - '0');
            i++;
        }

        exponent = exp_sign * exp_value;
    }

    result *= powf(10.0f, exponent);

    return (F32)(sign * result);
}

F64 Str8_ToF64(Str8 s) {
    if (!s.buffer || s.len == 0)
        return 0.0f;

    F64 result = 0.0;
    F64 fraction = 0.0;
    S32 exponent = 0;
    S32 sign = 1;
    F64 frac_divisor = 1;
    U64 i = 0;

    if (i < s.len && (s.buffer[i] == '-' || s.buffer[i] == '+')) {
        if (s.buffer[i] == '-') {
            sign = -1;
        }
        i++;
    }

    while (i < s.len && isdigit(s.buffer[i])) {
        result = result * 10.0 + (s.buffer[i] - '0');
        i++;
    }

    if (i < s.len && s.buffer[i] == '.') {
        i++;
        while (i < s.len && isdigit(s.buffer[i])) {
            fraction = fraction * 10.0 + (s.buffer[i] - '0');
            frac_divisor *= 10.0;
            i++;
        }
    }

    result += fraction / frac_divisor;

    if (i < s.len && (s.buffer[i] == 'e' || s.buffer[i] == 'E')) {
        i++;
        S32 exp_sign = 1;
        S32 exp_value = 0;

        if (i < s.len && (s.buffer[i] == '-' || s.buffer[i] == '+')) {
            if (s.buffer[i] == '-') {
                exp_sign = -1;
            }
            i++;
        }

        while (i < s.len && isdigit(s.buffer[i])) {
            exp_value = exp_value * 10 + (s.buffer[i] - '0');
            i++;
        }

        exponent = exp_sign * exp_value;
    }

    result *= pow(10.0, exponent);

    return sign * result;
}

Str8Array Str8_SplitWhitespace(Arena *arena, Str8 s) {
    U64 count = 0;
    U64 i = 0;
    while (i < s.len) {
        while (i < s.len && is_whitespace(s.buffer[i])) i++;
        if (i >= s.len) break;
        count++;
        while (i < s.len && !is_whitespace(s.buffer[i])) i++;
    }

    Str8Array res;
    res.len = count;
    res.elems = (Str8 *)Arena_Alloc(arena, count * sizeof(Str8));

    i = 0;
    U64 idx = 0;
    while (i < s.len) {
        while (i < s.len && is_whitespace(s.buffer[i])) i++;
        if (i >= s.len) break;
        U64 start = i;
        while (i < s.len && !is_whitespace(s.buffer[i])) i++;
        res.elems[idx++] = (Str8){ .buffer = s.buffer + start, .len = i - start };
    }

    return res;
}

Str8Array Str8_SplitChar(Arena *arena, Str8 s, U8 c) {
    U64 count = 0;
    for (U64 i = 0; i < s.len; ++i)
        if (s.buffer[i] == c)
            ++count;

    ++count;

    Str8Array res;
    res.len = count;
    res.elems = (Str8 *)Arena_Alloc(arena, count * sizeof(Str8));

    U64 start = 0;
    U64 elem_index = 0;
    for (U64 i = 0; i <= s.len; ++i) {
        if (i == s.len || s.buffer[i] == c) {
            U64 substr_len = i - start;
            res.elems[elem_index] = Str8_Substr(s, start, substr_len);
            ++elem_index;
            start = i + 1;
        }
    }

    return res;
}

Str8 Str8_JoinChar(Arena *arena, Str8Array arr, U8 sep) {
    if (arr.len == 0) {
        Str8 empty = {0};
        return empty;
    }

    U64 total_len = 0;
    for (U64 i = 0; i < arr.len; ++i)
        total_len += arr.elems[i].len;

    total_len += arr.len - 1;

    Str8 res;
    res.len = total_len;
    res.buffer = (U8 *)Arena_Alloc(arena, total_len);

    U64 pos = 0;
    for (U64 i = 0; i < arr.len; ++i) {
        memcpy(res.buffer + pos, arr.elems[i].buffer, arr.elems[i].len);
        pos += arr.elems[i].len;

        if (i < arr.len - 1) {
            res.buffer[pos] = sep;
            ++pos;
        }
    }

    return res;
}

Str8 Str8_JoinStr8(Arena *arena, Str8Array arr, Str8 sep) {
    if (arr.len == 0) {
        Str8 empty = {0};
        return empty;
    }

    U64 total_len = 0;
    for (U64 i = 0; i < arr.len; ++i)
        total_len += arr.elems[i].len;

    total_len += sep.len * (arr.len - 1);

    Str8 res;
    res.len = total_len;
    res.buffer = (U8 *)Arena_Alloc(arena, total_len);

    U64 pos = 0;
    for (U64 i = 0; i < arr.len; ++i) {
        memcpy(res.buffer + pos, arr.elems[i].buffer, arr.elems[i].len);
        pos += arr.elems[i].len;

        if (i < arr.len - 1) {
            memcpy(res.buffer + pos, sep.buffer, sep.len);
            pos += sep.len;
        }
    }

    return res;
}

B32 Str8File_Load(Str8File *file, char *path) {
#ifdef __unix__
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return false;

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return false;
    }

    file->size = sb.st_size;

    file->buffer = (U8 *)mmap(NULL, file->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file->buffer == MAP_FAILED) {
        close(fd);
        return false;
    }

    close(fd);

#else
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    file->size = ftell(f);
    rewind(f);

    file->buffer = malloc(file->size);
    if (!file->buffer) {
        fclose(f);
        return false;
    }

    size_t bytes_read = fread(file->buffer, 1, file->size, f);
    if (bytes_read != file->size) {
        free(file->buffer);
        fclose(f);
        return false;
    }

    fclose(f);
#endif

    file->offset = 0;

    return true;
}

Str8 Str8File_ReadLine(Str8File *file) {
    if (file->offset >= file->size)
        return (Str8){0};

    Str8 line;
    line.buffer = file->buffer + file->offset;
    line.len = 0;

    while (file->offset < file->size) {
        U8 ch = file->buffer[file->offset];

        if (ch == '\r') {
            if (file->offset + 1 < file->size && file->buffer[file->offset + 1] == '\n')
                file->offset += 2;
            else
                file->offset++;

            break;
        }

        if (ch == '\n') {
            file->offset++;
            break;
        }

        line.len++;
        file->offset++;
    }

    return line;
}

void Str8File_Close(Str8File *file) {
#ifdef __unix__
    munmap(file->buffer, file->size);
#else
    free(file->buffer);
#endif

    file->buffer = NULL;
    file->size   = 0;
    file->offset = 0;
}

#endif /* STR8_IMPLEMENTATION */
#endif /* STR8_H */
