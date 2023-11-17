#pragma once

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500L
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "arena.h"

static u8 *ut_memrchr(u8 *s, u8 c, u64 n) {
  pg_assert(s != NULL);
  pg_assert(n > 0);

  u8 *res = s + n - 1;
  while (res-- != s) {
    if (*res == c)
      return res;
  }
  return NULL;
}

// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
static u64 ut_next_power_of_two(u64 n) {
  n += !n; // Ensure n>0

  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  n++;
  return n;
}

static str_view_t str_view_new(u8 *s, u64 n) {
  return (str_view_t){.data = s, .len = n};
}

static str_view_t str_view_advance(str_view_t s, u64 n) {
  if (n > s.len)
    return (str_view_t){0};
  return (str_view_t){.data = s.data + n, .len = s.len - n};
}

static u8 str_view_first(str_view_t s) { return s.len > 0 ? s.data[0] : 0; }

static bool str_view_ends_with(str_view_t haystack, str_view_t needle) {
  if (needle.len > haystack.len)
    return false;

  uint64_t start = haystack.len - needle.len;
  return memcmp(&haystack.data[start], needle.data, needle.len) == 0;
}

static bool str_view_ends_with_c(str_view_t haystack, char *needle) {
  return str_view_ends_with(haystack, str_view_from_c(needle));
}

static bool str_view_is_empty(str_view_t s) { return s.len == 0; }

static bool str_view_eq(str_view_t a, str_view_t b) {
  if (a.len == 0 && b.len != 0)
    return false;
  if (b.len == 0 && a.len != 0)
    return false;

  if (a.len == 0 && b.len == 0)
    return true;

  pg_assert(a.data != NULL);
  pg_assert(b.data != NULL);

  return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

static bool str_view_eq_c(str_view_t a, char *b) {
  return str_view_eq(a, str_view_from_c(b));
}

static char *str_view_to_c(str_view_t s, arena_t *arena) {
  char *c_str = arena_alloc(arena, s.len + 1, 1, ALLOCATION_STRING);
  memcpy(c_str, s.data, s.len);

  c_str[s.len] = 0;

  return c_str;
}

static bool str_view_contains_element(str_view_t haystack, u8 needle) {
  for (int64_t i = 0; i < (int64_t)haystack.len - 1; i++) {
    if (haystack.data[i] == needle)
      return true;
  }
  return false;
}

typedef struct {
  str_view_t left, right;
  u64 found_pos;
  bool found;
  pg_pad(7);
} str_view_split_result_t;

static str_view_split_result_t str_view_split(str_view_t haystack, u8 needle) {
  uint8_t *at = memchr(haystack.data, needle, haystack.len);
  if (at == NULL)
    return (str_view_split_result_t){.left = haystack, .right = haystack};

  uint64_t found_pos = at - haystack.data;

  return (str_view_split_result_t){
      .left = (str_view_t){.data = haystack.data, .len = found_pos},
      .right =
          (str_view_t){.data = at + 1, .len = haystack.len - found_pos - 1},
      .found_pos = found_pos,
      .found = true,
  };
}

static str_view_split_result_t str_view_rsplit(str_view_t haystack, u8 needle) {
  uint8_t *at = ut_memrchr(haystack.data, needle, haystack.len);
  if (at == NULL)
    return (str_view_split_result_t){.left = haystack, .right = haystack};

  uint64_t found_pos = at - haystack.data;

  return (str_view_split_result_t){
      .left = (str_view_t){.data = haystack.data, .len = found_pos},
      .right =
          (str_view_t){.data = at + 1, .len = haystack.len - found_pos - 1},
      .found_pos = found_pos,
      .found = true,
  };
}

static u64 str_builder_space(str_builder_t sb) {
  pg_assert(sb.len < sb.cap);

  return sb.cap - sb.len - 1;
}

static str_builder_t str_builder_reserve_at_least(str_builder_t sb, u64 more,
                                                  arena_t *arena) {

  more += 1; // Null terminator.

  if (str_builder_space(sb) >= more)
    return sb;

  u64 new_cap = ut_next_power_of_two(str_builder_space(sb) + more);
  u8 *new_data = arena_alloc(arena, new_cap, 1, ALLOCATION_STRING);
  memcpy(new_data, sb.data, sb.len);

  return (str_builder_t){.len = sb.len, .cap = new_cap, .data = new_data};
}

static u8 *str_builder_end_c(str_builder_t sb) { return sb.data + sb.len; }

static str_builder_t str_builder_append(str_builder_t sb, str_view_t more,
                                        arena_t *arena) {
  sb = str_builder_reserve_at_least(sb, more.len, arena);
  memcpy(str_builder_end_c(sb), more.data, more.len);
  sb.data[sb.len + more.len] = 0;
  return (str_builder_t){
      .len = sb.len + more.len, .data = sb.data, .cap = sb.cap};
}

static str_builder_t str_builder_append_c(str_builder_t sb, char *more,
                                          arena_t *arena) {
  return str_builder_append(sb, str_view_from_c(more), arena);
}

static str_builder_t str_builder_append_element(str_builder_t sb, u8 c,
                                                arena_t *arena) {
  sb = str_builder_reserve_at_least(sb, 1, arena);
  sb.data[sb.len] = c;
  sb.data[sb.len + 1] = 0;
  return (str_builder_t){.len = sb.len + 1, .data = sb.data, .cap = sb.cap};
}

static str_builder_t str_builder_new(u64 initial_cap, arena_t *arena) {
  return (str_builder_t){
      .data = arena_alloc(arena, initial_cap + 1, 1, ALLOCATION_STRING),
      .cap = initial_cap + 1,
  };
}

static str_builder_t str_builder_assume_appended_n(str_builder_t sb, u64 more) {
  return (str_builder_t){.len = sb.len + more, .data = sb.data, .cap = sb.cap};
}

static str_view_t str_builder_build(str_builder_t sb) {
  return (str_view_t){.data = sb.data, .len = sb.len};
}

static str_builder_t str_builder_append_u64(str_builder_t sb, u64 n,
                                            arena_t *arena) {
  char tmp[25] = "";
  snprintf(tmp, sizeof(tmp) - 1, "%lu", n);
  return str_builder_append_c(sb, tmp, arena);
}

static str_builder_t str_builder_capitalize_at(str_builder_t sb, u64 pos) {
  pg_assert(pos < sb.len);
  
  if ('a' <= sb.data[pos] && sb.data[pos] <='z')
  sb.data[pos] -= 'a' - 'A';

  return sb;
}

static str_builder_t str_builder_clone(str_view_t src, arena_t *arena) {
  str_builder_t res = str_builder_new(src.len, arena);
  memcpy(res.data, src.data, src.len);
  res.len = src.len;
  return res;
}

static str_builder_t str_builder_replace_element_starting_at(str_builder_t sb,
                                                             u64 start, u8 from,
                                                             u8 to) {
  for (u64 i = start; i < sb.len; i++) {
    if (sb.data[i] == from)
      sb.data[i] = to;
  }
  return sb;
}
