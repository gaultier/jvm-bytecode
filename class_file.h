#pragma once

#include "arena.h"
#include "array.h"
#include "str.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

str *class_path_string_to_class_path_entries(str class_path, arena_t *arena) {
  pg_assert(!str_is_empty(class_path));

  str *entries = NULL;
  pg_array_init_reserve(entries, 16, arena);

  str_split_result_t split = {.right = class_path};
  do {
    split = str_split(split.right, ':');
    if (!str_is_empty(split.left))
      pg_array_append(entries, split.left, arena);
  } while (split.found);

  // Ensure "." is in the array so that it will be searched (but also do not
  // duplicate it).
  {
    bool found = false;
    str needle = str_from_c(".");

    for (u64 i = 0; i < pg_array_len(entries); i++) {
      if (str_eq(entries[i], needle)) {
        found = true;
        break;
      }
    }

    if (!found)
      pg_array_append(entries, needle, arena);
  }

  return entries;
}

// ------------------------ Class file code

typedef enum __attribute__((packed)) {
  BYTECODE_NOP = 0x00,
  BYTECODE_ALOAD_0 = 0x2a,
  BYTECODE_GET_STATIC = 0xb2,
  BYTECODE_BIPUSH = 0x10,
  BYTECODE_LDC = 0x12,
  BYTECODE_LDC_W = 0x13,
  BYTECODE_LDC2_W = 0x14,
  BYTECODE_ILOAD = 0x15,
  BYTECODE_LLOAD = 0x16,
  BYTECODE_ALOAD = 0x19,
  BYTECODE_LLOAD_0 = 0x1e,
  BYTECODE_LLOAD_1 = 0x1f,
  BYTECODE_LLOAD_2 = 0x20,
  BYTECODE_LLOAD_3 = 0x21,
  BYTECODE_ILOAD_0 = 0x1a,
  BYTECODE_ILOAD_1 = 0x1b,
  BYTECODE_ILOAD_3 = 0x1c,
  BYTECODE_ISTORE = 0x36,
  BYTECODE_LSTORE = 0x37,
  BYTECODE_ASTORE = 0x3a,
  BYTECODE_ISTORE_0 = 0x3b,
  BYTECODE_ISTORE_1 = 0x3c,
  BYTECODE_ISTORE_2 = 0x3d,
  BYTECODE_ISTORE_3 = 0x3e,
  BYTECODE_POP = 0x57,
  BYTECODE_IADD = 0x60,
  BYTECODE_LADD = 0x61,
  BYTECODE_IMUL = 0x68,
  BYTECODE_LMUL = 0x69,
  BYTECODE_IDIV = 0x6c,
  BYTECODE_LDIV = 0x6d,
  BYTECODE_IREM = 0x70,
  BYTECODE_LREM = 0x71,
  BYTECODE_INEG = 0x74,
  BYTECODE_LNEG = 0x75,
  BYTECODE_IAND = 0x7e,
  BYTECODE_LAND = 0x7f,
  BYTECODE_IOR = 0x80,
  BYTECODE_LOR = 0x80,
  BYTECODE_IXOR = 0x82,
  BYTECODE_I2L = 0x85,
  BYTECODE_LCMP = 0x94,
  BYTECODE_IFEQ = 0x99,
  BYTECODE_IFNE = 0x9a,
  BYTECODE_IF_ICMPEQ = 0x9f,
  BYTECODE_IF_ICMPNE = 0xa0,
  BYTECODE_IF_ICMPLT = 0xa1,
  BYTECODE_IF_ICMPGE = 0xa2,
  BYTECODE_IF_ICMPGT = 0xa3,
  BYTECODE_IF_ICMPLE = 0xa4,
  BYTECODE_GOTO = 0xa7,
  BYTECODE_IRETURN = 0xac,
  BYTECODE_LRETURN = 0xad,
  BYTECODE_RETURN = 0xb1,
  BYTECODE_INVOKE_VIRTUAL = 0xb6,
  BYTECODE_INVOKE_SPECIAL = 0xb7,
  BYTECODE_INVOKE_STATIC = 0xb8,
  BYTECODE_IMPDEP1 = 0xfe,
  BYTECODE_IMPDEP2 = 0xff,
} cf_op_kind_t;

typedef struct {
  u32 scope_depth;
  u32 var_definition_node_i;
  str name;
} ty_variable_t;

typedef enum __attribute__((packed)) {
  VERIFICATION_INFO_TOP = 0,
  VERIFICATION_INFO_INT = 1,
  VERIFICATION_INFO_FLOAT = 2,
  VERIFICATION_INFO_DOUBLE = 3,
  VERIFICATION_INFO_LONG = 4,
  VERIFICATION_INFO_NULL = 6,
  VERIFICATION_INFO_OBJECT = 7,
  VERIFICATION_INFO_UNINITIALIZED = 8,
} cf_verification_info_kind_t;

typedef struct {
  cf_verification_info_kind_t kind;
  pg_pad(1);
  u16 extra_data;
} cf_verification_info_t;

typedef struct {
  u32 node_i;
  u32 type_i;
  u32 scope_depth;
  cf_verification_info_t verification_info;
} cf_variable_t;

struct cg_frame_t;
typedef struct cg_frame_t cg_frame_t;
typedef struct {
  u16 pc;
  // TODO: Should we actually memoize this or not?
  u16 offset_delta;
  bool tombstone; // Skip in case of duplicates.
  u8 kind;
  pg_pad(2);
  // Immutable clone of the frame when the stack map
  // frame was created.
  cg_frame_t *frame;
} cf_stack_map_frame_t;

enum __attribute__((packed)) ty_type_kind_t {
  TYPE_ANY = 0,
  TYPE_UNIT = 1 << 0,
  TYPE_BOOLEAN = 1 << 1,
  TYPE_BYTE = 1 << 2,
  TYPE_CHAR = 1 << 3,
  TYPE_SHORT = 1 << 4,
  TYPE_INT = 1 << 5,
  TYPE_FLOAT = 1 << 6,
  TYPE_LONG = 1 << 7,
  TYPE_DOUBLE = 1 << 8,
  TYPE_STRING = 1 << 9,
  TYPE_METHOD = 1 << 10,
  TYPE_INSTANCE = 1 << 11,
  TYPE_ARRAY = 1 << 12,
  TYPE_INTEGER_LITERAL = 1 << 13,
  TYPE_CONSTRUCTOR = 1 << 14,
};
typedef enum ty_type_kind_t ty_type_kind_t;

static char *const CONSTRUCTOR_JVM_NAME = "<init>";

struct cg_frame_t {
  cf_variable_t *locals;
  cf_verification_info_t *stack;
  u16 max_physical_stack;
  u16 max_physical_locals;
  u32 scope_depth;
  u16 locals_physical_count;
  u16 stack_physical_count;
  pg_pad(4);
};

struct ty_type_t;

struct cf_constant_array_t;
typedef struct cf_constant_array_t cf_constant_array_t;

typedef struct {
  str name;
  str source_file_name;
  u8 *code;                           // In case of InlineOnly.
  cf_constant_array_t *constant_pool; // In case of InlineOnly.
  u32 *argument_types_i;
  u32 return_type_i;
  u32 this_class_type_i;
  // TODO: Move to `type.flags` to reduce size?
  u16 access_flags;
  u16 source_line;
  pg_pad(4);
} ty_type_method_t;

typedef enum {
  TYPE_FLAG_INLINE_ONLY = 1 << 10,
} ty_type_flag_t;

struct ty_type_t {
  str this_class_name;
  str super_class_name;
  str package_name;
  union {
    ty_type_method_t method;   // TYPE_METHOD, TYPE_CONSTRUCTOR
    u32 array_type_i;          // TYPE_ARRAY_REFERENCE
    u16 integer_literal_types; // TYPE_INTEGER_LITERAL: OR'ed integer types.
  } v;
  ty_type_kind_t kind;
  u16 flags;
  u32 super_type_i;
};

typedef struct ty_type_t ty_type_t;

typedef struct {
  u16 start_pc;
  u16 end_pc;
  u16 handler_pc;
  u16 catch_type;
} cf_exception_t;

typedef enum __attribute__((packed)) {
  AST_KIND_NONE,
  AST_KIND_NUMBER,
  AST_KIND_BOOL,
  AST_KIND_FUNCTION_DEFINITION,
  AST_KIND_FUNCTION_PARAMETER,
  AST_KIND_TYPE,
  AST_KIND_BINARY,
  AST_KIND_ASSIGNMENT,
  AST_KIND_THEN_ELSE,
  AST_KIND_UNARY,
  AST_KIND_VAR_DEFINITION,
  AST_KIND_VAR_REFERENCE,
  AST_KIND_CLASS_REFERENCE,
  AST_KIND_IF,
  AST_KIND_LIST,
  AST_KIND_WHILE_LOOP,
  AST_KIND_STRING,
  AST_KIND_NAVIGATION,
  AST_KIND_UNRESOLVED_NAME,
  AST_KIND_RETURN,
  AST_KIND_CALL,
  AST_KIND_MAX,
} par_ast_node_kind_t;

static str par_ast_node_kind_to_string[AST_KIND_MAX] = {
    [AST_KIND_NONE] = str_from_c_literal("NONE"),
    [AST_KIND_NUMBER] = str_from_c_literal("NUMBER"),
    [AST_KIND_BOOL] = str_from_c_literal("BOOL"),
    [AST_KIND_FUNCTION_DEFINITION] = str_from_c_literal("FUNCTION_DEFINITION"),
    [AST_KIND_FUNCTION_PARAMETER] = str_from_c_literal("FUNCTION_PARAMETER"),
    [AST_KIND_TYPE] = str_from_c_literal("TYPE"),
    [AST_KIND_BINARY] = str_from_c_literal("BINARY"),
    [AST_KIND_ASSIGNMENT] = str_from_c_literal("ASSIGNMENT"),
    [AST_KIND_THEN_ELSE] = str_from_c_literal("THEN_ELSE"),
    [AST_KIND_UNARY] = str_from_c_literal("UNARY"),
    [AST_KIND_VAR_DEFINITION] = str_from_c_literal("VAR_DEFINITION"),
    [AST_KIND_VAR_REFERENCE] = str_from_c_literal("VAR_REFERENCE"),
    [AST_KIND_CLASS_REFERENCE] = str_from_c_literal("CLASS_REFERENCE"),
    [AST_KIND_IF] = str_from_c_literal("IF"),
    [AST_KIND_LIST] = str_from_c_literal("LIST"),
    [AST_KIND_WHILE_LOOP] = str_from_c_literal("WHILE_LOOP"),
    [AST_KIND_STRING] = str_from_c_literal("STRING"),
    [AST_KIND_NAVIGATION] = str_from_c_literal("NAVIGATION"),
    [AST_KIND_UNRESOLVED_NAME] = str_from_c_literal("UNRESOLVED_NAME"),
    [AST_KIND_RETURN] = str_from_c_literal("RETURN"),
    [AST_KIND_CALL] = str_from_c_literal("CALL"),
};

// TODO: compact fields.
typedef struct {
  u32 main_token_i;
  u32 lhs;
  u32 rhs;
  u32 type_i; // TODO: should it be separate?
  // TODO: should it be separate?
  u32 *nodes; // AST_KIND_LIST
  u64 extra_data_i;
  u16 flags;
  par_ast_node_kind_t kind;
  pg_pad(5);
} par_ast_node_t;

typedef enum __attribute__((packed)) {
  PARSER_STATE_OK,
  PARSER_STATE_ERROR,
  PARSER_STATE_PANIC,
  PARSER_STATE_SYNCED,
} par_parser_state_t;

typedef enum __attribute__((packed)) {
  TOKEN_KIND_NONE,
  TOKEN_KIND_NUMBER,
  TOKEN_KIND_PLUS,
  TOKEN_KIND_MINUS,
  TOKEN_KIND_STAR,
  TOKEN_KIND_SLASH,
  TOKEN_KIND_PERCENT,
  TOKEN_KIND_AMPERSAND,
  TOKEN_KIND_AMPERSAND_AMPERSAND,
  TOKEN_KIND_PIPE,
  TOKEN_KIND_PIPE_PIPE,
  TOKEN_KIND_LEFT_PAREN,
  TOKEN_KIND_RIGHT_PAREN,
  TOKEN_KIND_LEFT_BRACE,
  TOKEN_KIND_RIGHT_BRACE,
  TOKEN_KIND_KEYWORD_FUN,
  TOKEN_KIND_KEYWORD_RETURN,
  TOKEN_KIND_KEYWORD_FALSE,
  TOKEN_KIND_KEYWORD_TRUE,
  TOKEN_KIND_KEYWORD_VAR,
  TOKEN_KIND_KEYWORD_IF,
  TOKEN_KIND_KEYWORD_ELSE,
  TOKEN_KIND_KEYWORD_WHILE,
  TOKEN_KIND_IDENTIFIER,
  TOKEN_KIND_EQUAL,
  TOKEN_KIND_COMMA,
  TOKEN_KIND_DOT,
  TOKEN_KIND_COLON,
  TOKEN_KIND_NOT,
  TOKEN_KIND_EQUAL_EQUAL,
  TOKEN_KIND_NOT_EQUAL,
  TOKEN_KIND_LE,
  TOKEN_KIND_LT,
  TOKEN_KIND_GE,
  TOKEN_KIND_GT,
  TOKEN_KIND_STRING_LITERAL,
} lex_token_kind_t;

typedef struct {
  u32 source_offset;
  lex_token_kind_t kind;
  pg_pad(3);
} lex_token_t;

typedef struct {
  str file_path;
  lex_token_t *tokens;
  u32 *line_table; // line_table[i] is the start offset of the LOC `i+1`
} lex_lexer_t;

typedef struct {
  str super_class_name;
  u32 this_class_type_i;
  pg_pad(4);
} resolver_super_class_to_resolve_t;

typedef struct {
  // FIXME: Use str_t
  str buf;
  lex_lexer_t *lexer;
  str current_package;
  par_ast_node_t *nodes;
  u32 current_function_i;
  u32 buf_len;
  u32 tokens_i;
  par_parser_state_t state;
  pg_pad(3);
} parser_t;

typedef struct {
  parser_t *parser;
  str this_class_name;
  str *class_path_entries;
  str *imported_package_names;
  ty_variable_t *variables;
  ty_type_t *types;
  u32 current_type_i;
  u32 scope_depth;
  u32 current_function_i;
  pg_pad(4);
} resolver_t;

static str cg_make_class_name_from_path(str path, arena_t *arena);

static void resolver_init(resolver_t *resolver, parser_t *parser,
                          str *class_path_entries, str class_file_path,
                          arena_t *arena) {

  resolver->parser = parser;
  resolver->class_path_entries = class_path_entries;
  resolver->this_class_name =
      cg_make_class_name_from_path(class_file_path, arena);

  pg_array_init_reserve(resolver->variables, 512, arena);
  pg_array_init_reserve(resolver->types, 1 << 18, arena);
  pg_array_init_reserve(resolver->imported_package_names, 256, arena);

  pg_array_append(resolver->imported_package_names, str_from_c("kotlin"),
                  arena);
  pg_array_append(resolver->imported_package_names,
                  str_from_c("kotlin.annotation"), arena);
  pg_array_append(resolver->imported_package_names,
                  str_from_c("kotlin.collections"), arena);
  pg_array_append(resolver->imported_package_names,
                  str_from_c("kotlin.comparisons"), arena);
  pg_array_append(resolver->imported_package_names, str_from_c("kotlin.io"),
                  arena);
  pg_array_append(resolver->imported_package_names, str_from_c("kotlin.ranges"),
                  arena);
  pg_array_append(resolver->imported_package_names,
                  str_from_c("kotlin.sequences"), arena);
  pg_array_append(resolver->imported_package_names, str_from_c("kotlin.text"),
                  arena);

  pg_array_append(resolver->imported_package_names, str_from_c("java.lang"),
                  arena);
  pg_array_append(resolver->imported_package_names, str_from_c("kotlin.jvm"),
                  arena);

  pg_array_append(resolver->imported_package_names, parser->current_package,
                  arena);
}

static void type_init_package_and_name(str fully_qualified_jvm_name,
                                       str *package_name, str *name,
                                       arena_t *arena) {
  str_split_result_t slash_rplit_res =
      str_rsplit(fully_qualified_jvm_name, '/');

  // No package component.
  if (!slash_rplit_res.found) {
    *name = str_clone(fully_qualified_jvm_name, arena);
    return;
  }

  str_builder package_name_builder = sb_clone(slash_rplit_res.left, arena);
  sb_replace_element_starting_at(package_name_builder, 0, '/', '.');
  *package_name = sb_build(package_name_builder);

  *name = str_clone(slash_rplit_res.right, arena);
}

static const ty_type_t *resolver_eval_type(resolver_t *resolver,
                                           const ty_type_t *in) {
  if (in->kind != TYPE_METHOD)
    return in;

  ty_type_t *const return_type = &resolver->types[in->v.method.return_type_i];

  return resolver_eval_type(resolver, return_type);
}

static bool resolver_is_type_integer(const ty_type_t *type) {
  switch (type->kind) {
  case TYPE_BYTE:
  case TYPE_SHORT:
  case TYPE_INT:
  case TYPE_LONG:
  case TYPE_INTEGER_LITERAL:
    return true;
  default:
    return false;
  }
}

static bool resolver_are_types_equal(const resolver_t *resolver,
                                     const ty_type_t *lhs,
                                     const ty_type_t *rhs) {
  if (lhs->kind != rhs->kind)
    return false;

  // Instances: Check the class name is the same.
  if (lhs->kind == TYPE_INSTANCE && rhs->kind == TYPE_INSTANCE) {
    return str_eq(lhs->this_class_name, rhs->this_class_name);
  }

  // Methods: Check that the class name, argument types, and return types, are
  // the same.
  if (lhs->kind == TYPE_METHOD && rhs->kind == TYPE_METHOD) {
    const ty_type_method_t *const lhs_method = &lhs->v.method;
    const ty_type_method_t *const rhs_method = &rhs->v.method;

    if (!str_eq(lhs->this_class_name, rhs->this_class_name))
      return false;

    if (!resolver_are_types_equal(resolver,
                                  &resolver->types[lhs_method->return_type_i],
                                  &resolver->types[rhs_method->return_type_i]))
      return false;

    if (pg_array_len(lhs_method->argument_types_i) !=
        pg_array_len(rhs_method->argument_types_i))
      return false;

    for (u64 i = 0; i < pg_array_len(lhs_method->argument_types_i); i++) {
      const u32 lhs_arg_i = lhs_method->argument_types_i[i];
      const u32 rhs_arg_i = rhs_method->argument_types_i[i];

      if (!resolver_are_types_equal(resolver, &resolver->types[lhs_arg_i],
                                    &resolver->types[rhs_arg_i]))
        return false;
    }

    return true;
  }

  // Otherwise, having the same `kind` is enough.
  return true;
}

static u16 resolver_widen_integer_type(const ty_type_t *type) {
  pg_assert(resolver_is_type_integer(type));

  if (type->kind == TYPE_INT) {
    return TYPE_INT | TYPE_LONG | TYPE_BYTE | TYPE_SHORT;
  } else if (type->kind == TYPE_SHORT) {
    return TYPE_BYTE | TYPE_SHORT;
  } else {
    return type->kind;
  }
}

static bool resolver_is_integer_type_subtype_of(const ty_type_t *a,
                                                const ty_type_t *b) {
  pg_assert(resolver_is_type_integer(a));

  // Every type is a subtype of Any.
  if (b->kind == TYPE_ANY)
    return true;

  // Integer types are subtypes of nothing else than Any (well to be exact they
  // are subtypes of Comparable but we do not implement interfaces yet).
  if (!resolver_is_type_integer(b))
    return false;

  // > All integer literal type are equivalent w.r.t. subtyping
  // > ILT(T1,…,TK)<:ILT(U1,…,UN)
  // > ILT(U1,…,UN)<:ILT(T1,…,TK)
  // > ∀Ti∈{T1,…,TK}:ILT(T1,…,TK)<:Ti
  // > ∀Ti∈{T1,…,TK}:Ti<:ILT(T1,…,TK)
  if (a->kind == TYPE_INTEGER_LITERAL || b->kind == TYPE_INTEGER_LITERAL)
    return true;

  return (resolver_widen_integer_type(a) & resolver_widen_integer_type(b)) ==
         resolver_widen_integer_type(b);
}

static bool resolver_resolve_super_lazily(resolver_t *resolver, ty_type_t *type,
                                          arena_t scratch_arena,
                                          arena_t *arena);

static bool resolver_is_type_subtype_of(resolver_t *resolver, ty_type_t *a,
                                        const ty_type_t *b,
                                        arena_t scratch_arena, arena_t *arena) {
  // `A <: A` always.
  if (resolver_are_types_equal(resolver, a, b))
    return true;

  // Every type is a subtype of Any.
  if (b->kind == TYPE_ANY)
    return true;

  // Integers have special rules.
  if (resolver_is_type_integer(a))
    return resolver_is_integer_type_subtype_of(a, b);

  switch (a->kind) {
    // Those types are not a subtype of anything (expect Any but we handled that
    // case already).
  case TYPE_METHOD:
  case TYPE_CONSTRUCTOR:
  case TYPE_DOUBLE:
  case TYPE_FLOAT:
  case TYPE_CHAR:
  case TYPE_ANY:
    return false;

  case TYPE_INSTANCE: {
    ty_type_t *it = a;
    while (true) {
      if (!resolver_resolve_super_lazily(resolver, it, scratch_arena, arena))
        return false;

      if (it->super_type_i == 0)
        return false; // Reached the top

      it = &resolver->types[it->super_type_i];
      if (resolver_are_types_equal(resolver, it, b))
        return true;
    }
    return false;
  }
  case TYPE_STRING: {
    return b->kind == TYPE_INSTANCE && str_eq_c(b->package_name, "java.lang") &&
           str_eq_c(b->this_class_name, "Object");
  }

  default:
    pg_assert(0 && "unreachable");
  }
}

static bool resolver_is_function_candidate_applicable(
    resolver_t *resolver, const u32 *call_site_argument_types_i,
    const ty_type_t *function_definition_type, arena_t scratch_arena,
    arena_t *arena) {
  pg_assert(function_definition_type->kind == TYPE_METHOD);

  const ty_type_method_t *definition = &function_definition_type->v.method;

  const u8 call_argument_count = pg_array_len(call_site_argument_types_i);
  const u8 definition_argument_count =
      pg_array_len(definition->argument_types_i);

  // Technically there is no such check in the spec but since we do not
  // implement varargs or default arguments yet, this will do for now.
  if (call_argument_count != definition_argument_count)
    return false;

  for (u8 i = 0; i < call_argument_count; i++) {
    const u32 call_site_argument_type_i = call_site_argument_types_i[i];
    ty_type_t *call_argument = &resolver->types[call_site_argument_type_i];
    const ty_type_t *definition_argument =
        &resolver->types[definition->argument_types_i[i]];

    const bool is_call_argument_subtype_of_definition_argument =
        resolver_is_type_subtype_of(resolver, call_argument,
                                    definition_argument, scratch_arena, arena);

    // TODO: Technically, we need to add the constraint `call argument <:
    // definition_argument` and afterwards check the soundness, but for now it
    // should do until we have generics (or varargs perhaps).
    if (!is_call_argument_subtype_of_definition_argument)
      return false;
  }

  // Per spec, the return type is not checked.

  return true;
}

static u32 resolver_add_type(resolver_t *resolver, ty_type_t *new_type,
                             arena_t *arena);

static u16
cf_verification_info_kind_word_count(cf_verification_info_kind_t kind) {
  switch (kind) {
  case VERIFICATION_INFO_TOP:
  case VERIFICATION_INFO_INT:
  case VERIFICATION_INFO_FLOAT:
  case VERIFICATION_INFO_NULL:
  case VERIFICATION_INFO_OBJECT:
  case VERIFICATION_INFO_UNINITIALIZED:
    return 1;
  case VERIFICATION_INFO_DOUBLE:
  case VERIFICATION_INFO_LONG:
    return 2;
  }
  pg_assert(0 && "unreachable");
}

static cf_verification_info_t
cg_type_to_verification_info(const ty_type_t *type) {

  switch (type->kind) {
  case TYPE_BOOLEAN:
  case TYPE_BYTE:
  case TYPE_CHAR:
  case TYPE_SHORT:
  case TYPE_INT:
    return (cf_verification_info_t){.kind = VERIFICATION_INFO_INT};
  case TYPE_LONG:
    return (cf_verification_info_t){.kind = VERIFICATION_INFO_LONG};
  case TYPE_FLOAT:
    return (cf_verification_info_t){.kind = VERIFICATION_INFO_FLOAT};
  case TYPE_DOUBLE:
    return (cf_verification_info_t){.kind = VERIFICATION_INFO_DOUBLE};
  case TYPE_INSTANCE:
  case TYPE_STRING:
    return (cf_verification_info_t){
        .kind = VERIFICATION_INFO_OBJECT,
        .extra_data = 0, // Patched later.
    };

  default:
    pg_assert(0 && "unreachable");
  }
}

static void cg_frame_stack_push(cg_frame_t *frame,
                                cf_verification_info_t verification_info,
                                arena_t *arena) {
  pg_assert(frame != NULL);
  pg_assert(arena != NULL);

  const u64 word_count =
      cf_verification_info_kind_word_count(verification_info.kind);

  pg_assert(frame->stack_physical_count + word_count < UINT16_MAX);
  pg_array_append(frame->stack, verification_info, arena);

  frame->stack_physical_count += word_count;
  frame->max_physical_stack =
      pg_max(frame->max_physical_stack, frame->stack_physical_count);
}

static void cg_frame_stack_pop(cg_frame_t *frame) {
  pg_assert(frame != NULL);
  pg_assert(pg_array_len(frame->stack) >= 1);
  pg_assert(frame->stack_physical_count >= 1);
  pg_assert(frame->max_physical_stack >= 1);

  pg_array_drop_last(frame->stack);

  frame->stack_physical_count -= 1;
}

static cg_frame_t *cg_frame_clone(const cg_frame_t *src, arena_t *arena);

static void cf_code_array_push_u8(u8 **array, u8 x, arena_t *arena) {
  pg_array_append(*array, x, arena);
}

static void cf_code_array_push_u16(u8 **array, u16 x, arena_t *arena) {
  cf_code_array_push_u8(array, (u8)((x & 0xff00) >> 8), arena);
  cf_code_array_push_u8(array, (u8)(x & 0x00ff), arena);
}

typedef enum __attribute__((packed)) {
  ACCESS_FLAGS_PUBLIC = 0x0001,
  ACCESS_FLAGS_PRIVATE = 0x0002,
  ACCESS_FLAGS_PROTECTED = 0x0004,
  ACCESS_FLAGS_STATIC = 0x0008,
  ACCESS_FLAGS_FINAL = 0x0010,
  ACCESS_FLAGS_SYNCHRONIZED = 0x0020,
  ACCESS_FLAGS_SUPER = 0x0020,
  ACCESS_FLAGS_VOLATILE = 0x0040,
  ACCESS_FLAGS_BRIDGE = 0x0040,
  ACCESS_FLAGS_TRANSIENT = 0x0080,
  ACCESS_FLAGS_VARARGS = 0x0080,
  ACCESS_FLAGS_NATIVE = 0x0100,
  ACCESS_FLAGS_INTERFACE = 0x0200,
  ACCESS_FLAGS_ABSTRACT = 0x0400,
  ACCESS_FLAGS_STRICT = 0x0800,
  ACCESS_FLAGS_SYNTHETIC = 0x1000,
  ACCESS_FLAGS_ANNOTATION = 0x2000,
  ACCESS_FLAGS_ENUM = 0x4000,
  ACCESS_FLAGS_MODULE = 0x8000,
} cf_access_flags_t;

typedef struct {
  union {
    u64 number;        // CONSTANT_POOL_KIND_INT
    str s;             // CONSTANT_POOL_KIND_UTF8
    u16 string_utf8_i; // CONSTANT_POOL_KIND_STRING
    struct cf_constant_method_ref_t {
      u16 class;
      u16 name_and_type;
    } method_ref;        // CONSTANT_POOL_KIND_METHOD_REF
    u16 java_class_name; // CONSTANT_POOL_KIND_CLASS_INFO
    struct cf_constant_name_and_type_t {
      u16 name;
      u16 descriptor;
    } name_and_type; // CONSTANT_POOL_KIND_NAME_AND_TYPE
    struct cf_constant_field_ref_t {
      u16 name;
      u16 descriptor;
    } field_ref; // CONSTANT_POOL_KIND_FIELD_REF
  } v;
  enum __attribute__((packed)) cp_info_kind_t {
    CONSTANT_POOL_KIND_UTF8 = 1,
    CONSTANT_POOL_KIND_INT = 3,
    CONSTANT_POOL_KIND_FLOAT = 4,
    CONSTANT_POOL_KIND_LONG = 5,
    CONSTANT_POOL_KIND_DOUBLE = 6,
    CONSTANT_POOL_KIND_CLASS_INFO = 7,
    CONSTANT_POOL_KIND_STRING = 8,
    CONSTANT_POOL_KIND_FIELD_REF = 9,
    CONSTANT_POOL_KIND_METHOD_REF = 10,
    CONSTANT_POOL_KIND_INTERFACE_METHOD_REF = 11,
    CONSTANT_POOL_KIND_NAME_AND_TYPE = 12,
    CONSTANT_POOL_KIND_METHOD_HANDLE = 15,
    CONSTANT_POOL_KIND_METHOD_TYPE = 16,
    CONSTANT_POOL_KIND_DYNAMIC = 17,
    CONSTANT_POOL_KIND_INVOKE_DYNAMIC = 18,
    CONSTANT_POOL_KIND_MODULE = 19,
    CONSTANT_POOL_KIND_PACKAGE = 20,
  } kind;
  pg_pad(7);
} cf_constant_t;

typedef struct cf_constant_method_ref_t cf_constant_method_ref_t;

typedef struct cf_constant_name_and_type_t cf_constant_name_and_type_t;

typedef struct cf_constant_field_ref_t cf_constant_field_ref_t;

typedef enum cp_info_kind_t cp_info_kind_t;

struct cf_constant_array_t {
  u64 len;
  u64 cap;
  cf_constant_t *values;
};

static cf_constant_array_t cf_constant_array_make(u64 cap, arena_t *arena) {
  pg_assert(arena != NULL);

  return (cf_constant_array_t){
      .len = 0,
      .cap = cap,
      .values = arena_alloc(arena, sizeof(cf_constant_t),
                            _Alignof(cf_constant_t), cap),
  };
}

static u16 cf_constant_array_push(cf_constant_array_t *array,
                                  const cf_constant_t *x, arena_t *arena) {
  pg_assert(array != NULL);
  pg_assert(x != NULL);
  pg_assert(array->len < UINT16_MAX);
  pg_assert(array->values != NULL);
  pg_assert(((u64)(array->values)) % 8 == 0);
  pg_assert(array->cap != 0);

  if (array->len == array->cap) {
    const u64 new_cap = array->cap * 2;
    cf_constant_t *const new_array = arena_alloc(
        arena, sizeof(cf_constant_t), _Alignof(cf_constant_t), new_cap);
    pg_assert(new_array != NULL);
    array->values =
        memcpy(new_array, array->values, array->len * sizeof(cf_constant_t));
    pg_assert(array->values != NULL);
    pg_assert(((u64)(array->values)) % 16 == 0);
    array->cap = new_cap;
  }

  array->values[array->len] = *x;
  const u64 index = array->len + 1;
  pg_assert(index > 0);
  pg_assert(index <= array->len + 1);
  array->len += 1;
  return index;
}

static void cg_frame_init(cg_frame_t *frame, arena_t *arena) {
  pg_assert(frame != NULL);
  pg_assert(arena != NULL);

  pg_array_init_reserve(frame->locals, 32, arena);
  pg_array_init_reserve(frame->stack, 256, arena);
}

static cg_frame_t *cg_frame_clone(const cg_frame_t *src, arena_t *arena) {
  pg_assert(src != NULL);
  pg_assert(src->stack != NULL);
  pg_assert(pg_array_len(src->stack) <= UINT16_MAX);
  pg_assert(src->locals != NULL);
  pg_assert(arena != NULL);

  cg_frame_t *dst =
      arena_alloc(arena, sizeof(cg_frame_t), _Alignof(cg_frame_t), 1);
  cg_frame_init(dst, arena);

  dst->max_physical_stack = src->max_physical_stack;
  dst->max_physical_locals = src->max_physical_locals;
  dst->scope_depth = src->scope_depth;
  dst->stack_physical_count = src->stack_physical_count;
  dst->locals_physical_count = src->locals_physical_count;

  for (u64 i = 0; i < pg_array_len(src->locals); i++)
    pg_array_append(dst->locals, src->locals[i], arena);

  for (u64 i = 0; i < pg_array_len(src->stack); i++)
    pg_array_append(dst->stack, src->stack[i], arena);

  pg_assert(pg_array_len(dst->locals) == pg_array_len(src->locals));
  pg_assert(pg_array_len(dst->stack) == pg_array_len(src->stack));

  return dst;
}

static cf_constant_array_t *
cf_constant_array_clone(const cf_constant_array_t *constant_pool,
                        arena_t *arena) {
  cf_constant_array_t *res = arena_alloc(arena, sizeof(cf_constant_array_t),
                                         _Alignof(cf_constant_array_t), 1);
  res->cap = res->len = constant_pool->len;
  res->values = arena_alloc(arena, sizeof(cf_constant_t),
                            _Alignof(cf_constant_t), constant_pool->len);

  for (u64 i = 0; i < res->len; i++) {
    const cf_constant_t constant = constant_pool->values[i];
    res->values[i] = constant;

    if (constant.kind == CONSTANT_POOL_KIND_UTF8) {
      res->values[i].v.s = constant.v.s;
    }
  }

  return res;
}

static const cf_constant_t *
cf_constant_array_get(const cf_constant_array_t *constant_pool, u16 i) {
  pg_assert(constant_pool != NULL);
  pg_assert(i > 0);
  pg_assert(i <= constant_pool->len);
  pg_assert(constant_pool->values != NULL);
  pg_assert(((u64)(constant_pool->values)) % 8 == 0);

  return &constant_pool->values[i - 1];
}

static str_builder cf_fill_descriptor_string(const ty_type_t *types,
                                             str_builder sb, u32 type_i,
                                             arena_t *arena) {
  pg_assert(types != NULL);
  pg_assert(type_i < pg_array_len(types));

  const ty_type_t *const type = &types[type_i];

  switch (type->kind) {
  case TYPE_UNIT: {
    return sb_append_char(sb, 'V', arena);
  }
  case TYPE_BYTE: {
    return sb_append_char(sb, 'B', arena);
  }
  case TYPE_CHAR: {
    return sb_append_char(sb, 'C', arena);
  }
  case TYPE_DOUBLE: {
    return sb_append_char(sb, 'D', arena);
  }
  case TYPE_FLOAT: {
    return sb_append_char(sb, 'F', arena);
  }
  case TYPE_INT: {
    return sb_append_char(sb, 'I', arena);
  }
  case TYPE_LONG: {
    return sb_append_char(sb, 'J', arena);
  }
  case TYPE_STRING: {
    return sb_append_c(sb, "Ljava/lang/String;", arena);
  }
  case TYPE_INSTANCE: {
    str java_class_name = type->this_class_name;

    sb = sb_append_char(sb, 'L', arena);
    sb = sb_append(sb, java_class_name, arena);
    return sb_append_char(sb, ';', arena);
  }
  case TYPE_SHORT: {
    return sb_append_char(sb, 'S', arena);
  }
  case TYPE_BOOLEAN: {
    return sb_append_char(sb, 'Z', arena);
  }
  case TYPE_ARRAY: {
    sb = sb_append_char(sb, '[', arena);
    return cf_fill_descriptor_string(types, sb, type->v.array_type_i, arena);
  }
  case TYPE_METHOD:
  case TYPE_CONSTRUCTOR: {
    const ty_type_method_t *const method_type = &type->v.method;
    sb = sb_append_char(sb, '(', arena);

    for (u64 i = 0; i < pg_array_len(method_type->argument_types_i); i++) {
      sb = cf_fill_descriptor_string(types, sb,
                                     method_type->argument_types_i[i], arena);
    }

    sb = sb_append_char(sb, ')', arena);

    return cf_fill_descriptor_string(types, sb, method_type->return_type_i,
                                     arena);
  }
  default:
    pg_assert(0 && "unreachable");
  }
}

static str cf_parse_descriptor(resolver_t *resolver, str descriptor,
                               ty_type_t *type, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(type != NULL);
  pg_assert(arena != NULL);

  if (str_is_empty(descriptor))
    return (str){0};

  str remaining = descriptor;

  switch (remaining.data[0]) {
  case 'V': {
    type->kind = TYPE_UNIT;
    return str_advance(remaining, 1);
  }

  case 'S': {
    type->kind = TYPE_SHORT;

    return str_advance(remaining, 1);
  }

  case 'B': {
    type->kind = TYPE_BYTE;

    return str_advance(remaining, 1);
  }

  case 'C': {
    type->kind = TYPE_CHAR;

    return str_advance(remaining, 1);
  }

  case 'D': {
    type->kind = TYPE_DOUBLE;

    return str_advance(remaining, 1);
  }

  case 'F': {
    type->kind = TYPE_FLOAT;

    return str_advance(remaining, 1);
  }

  case 'I': {
    type->kind = TYPE_INT;

    return str_advance(remaining, 1);
  }

  case 'J': {
    type->kind = TYPE_LONG;

    return str_advance(remaining, 1);
  }

  case 'Z': {
    type->kind = TYPE_BOOLEAN;

    return str_advance(remaining, 1);
  }

  case 'L': {
    remaining = str_advance(remaining, 1);
    str_split_result_t semicolon_split = str_split(remaining, ';');
    pg_assert(semicolon_split.found);
    str fqn = semicolon_split.left;

    if (str_eq_c(type->this_class_name, "java/lang/String")) {
      type->kind = TYPE_STRING;
    } else {
      type->kind = TYPE_INSTANCE;
    }
    type_init_package_and_name(fqn, &type->package_name, &type->this_class_name,
                               arena);

    return semicolon_split.right;
  }

  case '[': {
    type->kind = TYPE_ARRAY;
    ty_type_t item_type = {0};

    str descriptor_remaining = {
        .data = remaining.data + 1,
        .len = remaining.len - 1,
    };
    remaining =
        cf_parse_descriptor(resolver, descriptor_remaining, &item_type, arena);

    if (!str_is_empty(item_type.this_class_name)) {
      type_init_package_and_name(item_type.this_class_name, &type->package_name,
                                 &type->this_class_name, arena);
    }

    type->v.array_type_i = resolver_add_type(resolver, &item_type, arena);
    return remaining;
  }

  case '(': {
    // Might be: TYPE_CONSTRUCTOR, but we cannot know based on the type
    // descriptor, only based on the name.
    // Hence, the caller will have to patch the kind afterwards.
    type->kind = TYPE_METHOD;
    remaining = str_advance(remaining, 1);

    u32 *argument_types_i = NULL;
    pg_array_init_reserve(argument_types_i, remaining.len, arena);

    for (u64 i = 0; i < 255; i++) {
      if (str_first(remaining) == ')')
        break;

      ty_type_t argument_type = {0};
      remaining =
          cf_parse_descriptor(resolver, remaining, &argument_type, arena);
      const u32 argument_type_i =
          resolver_add_type(resolver, &argument_type, arena);
      pg_array_append(argument_types_i, argument_type_i, arena);
    }
    pg_assert(remaining.len >= 1);
    pg_assert(remaining.data[0] = ')');
    remaining = str_advance(remaining, 1);

    ty_type_t return_type = {0};
    remaining = cf_parse_descriptor(resolver, remaining, &return_type, arena);
    // TODO: Check cache before adding the type.

    type->v.method.argument_types_i = argument_types_i;
    type->v.method.return_type_i =
        resolver_add_type(resolver, &return_type, arena);

    return remaining;
  }
  default:
    pg_assert(0 && "unreachable");
  }

  __builtin_unreachable();
}

struct cf_annotation_t;

typedef struct {
  u16 type_name_index;
  u16 const_name_index;
} cf_enum_const_value_t;

struct cf_element_value_t {
  union {
    u16 const_value_index;
    cf_enum_const_value_t enum_const_value;
    u16 class_info_index;
    struct cf_annotation_t *annotation_value;
    struct cf_element_value_t *array_value;
  } v;
  u8 tag;
  pg_pad(7);
};
typedef struct cf_element_value_t cf_element_value_t;

typedef struct {
  u16 element_name_index;
  pg_pad(6);
  cf_element_value_t element_value;
} cf_element_value_pair_t;

struct cf_annotation_t {
  u16 type_index;
  pg_pad(6);
  cf_element_value_pair_t *element_value_pairs;
};
typedef struct cf_annotation_t cf_annotation_t;

typedef struct cf_attribute_t cf_attribute_t;

typedef struct {
  u16 access_flags;
  u16 name;
  u16 descriptor;
  pg_pad(2);
  cf_attribute_t *attributes;
} cf_field_t;

typedef struct cf_method_t cf_method_t;

typedef struct cf_interfaces_t cf_interfaces_t;

typedef struct {
  u16 start_pc;
  u16 line_number;
} cf_line_number_table_entry_t;

struct cf_attribute_t {
  union {
    struct cf_attribute_code_t {
      u16 max_physical_stack;
      u16 max_physical_locals;
      pg_pad(4);
      u8 *code;
      cf_exception_t *exceptions;
      cf_attribute_t *attributes;
    } code; // ATTRIBUTE_KIND_CODE

    struct cf_attribute_source_file_t {
      u16 source_file;
    } source_file; // ATTRIBUTE_KIND_SOURCE_FILE

    cf_line_number_table_entry_t
        *line_number_table_entries; // ATTRIBUTE_KIND_LINE_NUMBER_TABLE

    cf_stack_map_frame_t *stack_map_table; // ATTRIBUTE_KIND_STACK_MAP_TABLE
    cf_annotation_t *
        runtime_invisible_annotations; // ATTRIBUTE_KIND_RUNTIME_INVISIBLE_ANNOTATIONS

    u16 *exception_index_table; // ATTRIBUTE_KIND_EXCEPTIONS
  } v;
  u16 name;
  enum __attribute__((packed)) cf_attribute_kind_t {
    ATTRIBUTE_KIND_SOURCE_FILE,
    ATTRIBUTE_KIND_CODE,
    ATTRIBUTE_KIND_LINE_NUMBER_TABLE,
    ATTRIBUTE_KIND_STACK_MAP_TABLE,
    ATTRIBUTE_KIND_RUNTIME_INVISIBLE_ANNOTATIONS,
    ATTRIBUTE_KIND_EXCEPTIONS,
  } kind;
  pg_pad(5);
};

typedef enum cf_attribute_kind_t cf_attribute_kind_t;

typedef struct cf_attribute_line_number_table_t
    cf_attribute_line_number_table_t;

typedef struct cf_attribute_code_t cf_attribute_code_t;

typedef struct cf_attribute_source_file_t cf_attribute_source_file_t;

struct cf_method_t {
  u16 access_flags;
  u16 name;
  u16 descriptor;
  pg_pad(2);
  cf_attribute_t *attributes;
};

const u32 cf_MAGIC_NUMBER = 0xbebafeca;
const u16 cf_MAJOR_VERSION_6 = 50;
const u16 cf_MAJOR_VERSION_7 = 51;
const u16 cf_MINOR_VERSION = 0;

struct cf_class_file_t {
  str archive_file_path;
  str class_file_path;
  str class_name;
  u16 minor_version;
  u16 major_version;
  u16 access_flags;
  u16 this_class;
  u16 super_class;
  u16 interfaces_count;
  u16 fields_count;
  pg_pad(2);
  u16 *interfaces;
  cf_field_t *fields;
  cf_method_t *methods;
  cf_attribute_t *attributes;
  cf_constant_array_t constant_pool;
};
typedef struct cf_class_file_t cf_class_file_t;

static void file_write_u8(FILE *file, u8 x) {
  pg_assert(file != NULL);
  fwrite(&x, sizeof(x), 1, file);
}
static void file_write_be_u16(FILE *file, u16 x) {
  pg_assert(file != NULL);

  const u8 x_be[2] = {
      (x & 0xff00) >> 8,
      (x & 0x00ff) >> 0,
  };
  fwrite(x_be, sizeof(x_be), 1, file);
}

static void file_write_be_u32(FILE *file, u32 x) {
  pg_assert(file != NULL);

  const u8 x_be[4] = {
      (x & 0xff000000) >> 24,
      (x & 0x00ff0000) >> 16,
      (x & 0x0000ff00) >> 8,
      (x & 0x000000ff) >> 0,
  };
  fwrite(x_be, sizeof(x_be), 1, file);
}

static void file_write_be_u64(FILE *file, u64 x) {
  pg_assert(file != NULL);

  const u8 x_be[8] = {
      (x & 0xfff0000000000000) >> 56, (x & 0x00ff000000000000) >> 48,
      (x & 0x0000ff0000000000) >> 40, (x & 0x000000ff00000000) >> 32,
      (x & 0x00000000ff000000) >> 24, (x & 0x0000000000ff0000) >> 16,
      (x & 0x000000000000ff00) >> 8,  (x & 0x00000000000000ff) >> 0,
  };
  fwrite(x_be, sizeof(x_be), 1, file);
}

static u16 buf_read_be_u16(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + sizeof(u16) <= buf.data + buf.len);

  const u8 *const ptr = *current;
  const u16 x = (((u16)(ptr[0] & 0xff)) << 8) | ((u16)((ptr[1] & 0xff)) << 0);
  *current += sizeof(u16);
  return x;
}

static u16 buf_read_le_u16(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + sizeof(u16) <= buf.data + buf.len);

  const u8 *const ptr = *current;
  const u16 x = (((u16)(ptr[1] & 0xff)) << 8) | ((u16)((ptr[0] & 0xff)) << 0);
  *current += sizeof(u16);
  return x;
}

static u32 buf_read_be_u32(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + sizeof(u32) <= buf.data + buf.len);

  const u8 *const ptr = *current;
  const u32 x = ((u32)(ptr[0] & 0xff) << 24) | (((u32)(ptr[1] & 0xff)) << 16) |
                (((u32)(ptr[2] & 0xff)) << 8) | (((u32)(ptr[3] & 0xff)) << 0);
  *current += sizeof(u32);
  return x;
}

static u32 buf_read_le_u32(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + sizeof(u32) <= buf.data + buf.len);

  const u8 *const ptr = *current;
  const u32 x = ((u32)(ptr[3] & 0xff) << 24) | (((u32)(ptr[2] & 0xff)) << 16) |
                (((u32)(ptr[1] & 0xff)) << 8) | (((u32)(ptr[0] & 0xff)) << 0);
  *current += sizeof(u32);
  return x;
}

static str buf_read_n_u8(str buf, u64 n, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + n <= buf.data + buf.len);

  str res = {.data = *current, .len = n};
  *current += n;

  return res;
}

static u8 buf_read_u8(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + sizeof(u8) <= buf.data + buf.len);

  const u8 x = (*current)[0];
  *current += 1;
  return x;
}

static str
cf_constant_array_get_as_string(const cf_constant_array_t *constant_pool,
                                u16 i) {
  const cf_constant_t *const constant = cf_constant_array_get(constant_pool, i);
  pg_assert(constant->kind == CONSTANT_POOL_KIND_UTF8);
  return constant->v.s;
}

static void cf_buf_read_attributes(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   cf_attribute_t **attributes, arena_t *arena);

static void cf_buf_read_sourcefile_attribute(str buf, u8 **current,
                                             cf_class_file_t *class_file,
                                             cf_attribute_t **attributes,
                                             arena_t *arena) {

  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(attributes != NULL);
  pg_assert(*attributes != NULL);

  const u8 *const current_start = *current;

  cf_attribute_source_file_t source_file = {0};
  source_file.source_file = buf_read_be_u16(buf, current);
  pg_assert(source_file.source_file > 0);
  pg_assert(source_file.source_file <= class_file->constant_pool.len);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == 2);

  cf_attribute_t attribute = {.kind = ATTRIBUTE_KIND_SOURCE_FILE,
                              .v = {.source_file = source_file}};
  pg_array_append(*attributes, attribute, arena);
}

static void cf_buf_read_code_attribute_exceptions(str buf, u8 **current,
                                                  cf_class_file_t *class_file,
                                                  cf_exception_t **exceptions,
                                                  arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(exceptions != NULL);

  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  pg_array_init_reserve(*exceptions, table_len, arena);

  for (u16 i = 0; i < table_len; i++) {
    cf_exception_t exception = {0};

    exception.start_pc = buf_read_be_u16(buf, current);
    exception.end_pc = buf_read_be_u16(buf, current);
    exception.handler_pc = buf_read_be_u16(buf, current);
    exception.catch_type = buf_read_be_u16(buf, current);

    pg_array_append(*exceptions, exception, arena);
  }

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == sizeof(u16) + table_len * sizeof(u16) * 4);
}

static void cf_buf_read_code_attribute(str buf, u8 **current,
                                       cf_class_file_t *class_file,
                                       u32 attribute_len, u16 name_i,
                                       cf_attribute_t **attributes,
                                       arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current + 2 <= buf.data + buf.len);
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(attributes != NULL);
  pg_assert(arena != NULL);

  const u8 *const current_start = *current;

  cf_attribute_code_t code = {0};
  code.max_physical_stack = buf_read_be_u16(buf, current);
  code.max_physical_locals = buf_read_be_u16(buf, current);
  const u32 code_len = buf_read_be_u32(buf, current);
  pg_assert(*current + code_len <= buf.data + buf.len);
  pg_assert(code_len <= UINT16_MAX); // Actual limit per spec.

  pg_array_init_reserve(code.code, code_len, arena);
  str code_slice = buf_read_n_u8(buf, code_len, current);
  memcpy(code.code, code_slice.data, code_slice.len);
  pg_array_header(code.code)->len = code_len;

  cf_buf_read_code_attribute_exceptions(buf, current, class_file,
                                        &code.exceptions, arena);

  cf_buf_read_attributes(buf, current, class_file, &code.attributes, arena);

  cf_attribute_t attribute = {
      .kind = ATTRIBUTE_KIND_CODE, .name = name_i, .v = {.code = code}};
  pg_array_append(*attributes, attribute, arena);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void
cf_buf_read_stack_map_table_attribute_verification_infos(str buf, u8 **current,
                                                         u16 count) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);

  for (u16 i = 0; i < count; i++) {
    const u8 kind = buf_read_u8(buf, current);

    if (kind < 7)
      continue;

    if (kind > 8)
      pg_assert(0 && "invalid");

    buf_read_be_u16(buf, current);
  }
}

static void cf_buf_read_stack_map_table_attribute(str buf, u8 **current,
                                                  u32 attribute_len, u16 name_i,
                                                  cf_attribute_t **attributes,
                                                  arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(attributes != NULL);
  pg_assert(arena != NULL);

  const u8 *const current_start = *current;

  const u16 len = buf_read_be_u16(buf, current);
  cf_stack_map_frame_t *stack_map_frames = NULL;
  pg_array_init_reserve(stack_map_frames, len, arena);

  for (u16 i = 0; i < len; i++) {
    cf_stack_map_frame_t stack_map_frame = {
        .kind = buf_read_u8(buf, current),
    };

    if (stack_map_frame.kind <= 63) // same_frame
    {
      stack_map_frame.offset_delta = stack_map_frame.kind;
    } else if (64 <= stack_map_frame.kind &&
               stack_map_frame.kind <= 127) { // same_locals_1_stack_item_frame
      stack_map_frame.offset_delta = stack_map_frame.kind - 64;
      cf_buf_read_stack_map_table_attribute_verification_infos(buf, current, 1);

    } else if (128 <= stack_map_frame.kind &&
               stack_map_frame.kind <= 246) { // reserved
      pg_assert(0 && "unreachable");
    } else if (247 <= stack_map_frame.kind &&
               stack_map_frame.kind <=
                   247) { // same_locals_1_stack_item_frame_extended
      stack_map_frame.offset_delta = buf_read_be_u16(buf, current);
      cf_buf_read_stack_map_table_attribute_verification_infos(buf, current, 1);

    } else if (248 <= stack_map_frame.kind &&
               stack_map_frame.kind <= 250) { // chop_frame
      stack_map_frame.offset_delta = buf_read_be_u16(buf, current);

    } else if (251 <= stack_map_frame.kind &&
               stack_map_frame.kind <= 251) { // same_frame_extended
      stack_map_frame.offset_delta = buf_read_be_u16(buf, current);

    } else if (252 <= stack_map_frame.kind &&
               stack_map_frame.kind <= 254) { // append_frame
      stack_map_frame.offset_delta = buf_read_be_u16(buf, current);
      const u16 verification_info_count = stack_map_frame.kind - 251;
      cf_buf_read_stack_map_table_attribute_verification_infos(
          buf, current, verification_info_count);

    } else { // full_frame_attribute
      stack_map_frame.offset_delta = buf_read_be_u16(buf, current);
      const u16 locals_count = buf_read_be_u16(buf, current);
      cf_buf_read_stack_map_table_attribute_verification_infos(buf, current,
                                                               locals_count);
      const u16 stack_items_count = buf_read_be_u16(buf, current);
      cf_buf_read_stack_map_table_attribute_verification_infos(
          buf, current, stack_items_count);
    }
    pg_array_append(stack_map_frames, stack_map_frame, arena);
  }

  cf_attribute_t attribute = {
      .kind = ATTRIBUTE_KIND_STACK_MAP_TABLE,
      .name = name_i,
      .v = {.stack_map_table = stack_map_frames},
  };
  pg_array_append(*attributes, attribute, arena);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_line_number_table_attribute(str buf, u8 **current,
                                                    cf_class_file_t *class_file,
                                                    u32 attribute_len,
                                                    cf_attribute_t **attributes,
                                                    arena_t *arena) {
  pg_unused(arena);
  pg_unused(class_file);

  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  pg_assert(sizeof(table_len) + table_len * (sizeof(u16) + sizeof(u16)) ==
            attribute_len);

  cf_attribute_t attribute = {.kind = ATTRIBUTE_KIND_LINE_NUMBER_TABLE};
  pg_array_init_reserve(attribute.v.line_number_table_entries, table_len,
                        arena);

  for (u16 i = 0; i < table_len; i++) {
    cf_line_number_table_entry_t entry = {
        .start_pc = buf_read_be_u16(buf, current),
        .line_number = buf_read_be_u16(buf, current),
    };
    pg_array_append(attribute.v.line_number_table_entries, entry, arena);
  }

  pg_array_append(*attributes, attribute, arena);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void
cf_buf_read_local_variable_table_attribute(str buf, u8 **current,
                                           cf_class_file_t *class_file,
                                           u32 attribute_len, arena_t *arena) {
  pg_unused(arena);
  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  const u16 entry_size = sizeof(u16) * 5;
  pg_assert(sizeof(table_len) + table_len * entry_size == attribute_len);

  for (u16 i = 0; i < table_len; i++) {
    const u16 start_pc = buf_read_be_u16(buf, current);
    pg_unused(start_pc);
    const u16 len = buf_read_be_u16(buf, current);
    pg_unused(len);
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= class_file->constant_pool.len);

    const u16 descriptor_i = buf_read_be_u16(buf, current);
    pg_unused(descriptor_i);
    const u16 idx = buf_read_be_u16(buf, current);
    pg_unused(idx);

    // TODO store.
  }
  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_local_variable_type_table_attribute(
    str buf, u8 **current, cf_class_file_t *class_file, u32 attribute_len,
    arena_t *arena) {
  pg_unused(arena);
  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  const u16 entry_size = sizeof(u16) * 5;
  pg_assert(sizeof(table_len) + table_len * entry_size == attribute_len);

  for (u16 i = 0; i < table_len; i++) {
    const u16 start_pc = buf_read_be_u16(buf, current);
    pg_unused(start_pc);
    const u16 len = buf_read_be_u16(buf, current);
    pg_unused(len);
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= class_file->constant_pool.len);

    const u16 signature_i = buf_read_be_u16(buf, current);
    pg_unused(signature_i);
    const u16 idx = buf_read_be_u16(buf, current);
    pg_unused(idx);

    // TODO store.
  }
  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_signature_attribute(str buf, u8 **current,
                                            cf_class_file_t *class_file,
                                            u32 attribute_len, arena_t *arena) {
  pg_unused(arena);
  pg_unused(class_file);

  const u8 *const current_start = *current;

  pg_assert(attribute_len == 2);
  const u16 signature_i = buf_read_be_u16(buf, current);
  pg_unused(signature_i);
  // TODO store.

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

// TODO: store this data.
static void cf_buf_read_exceptions_attribute(str buf, u8 **current,
                                             cf_class_file_t *class_file,
                                             u32 attribute_len,
                                             cf_attribute_t **attributes,
                                             arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(attributes != NULL);
  pg_assert(arena != NULL);

  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  const u16 entry_size = sizeof(u16);
  pg_assert(sizeof(table_len) + table_len * entry_size == attribute_len);

  cf_attribute_t attribute = {.kind = ATTRIBUTE_KIND_EXCEPTIONS};
  pg_array_init_reserve(attribute.v.exception_index_table, table_len, arena);

  for (u16 i = 0; i < table_len; i++) {
    const u16 exception_i = buf_read_be_u16(buf, current);
    pg_assert(exception_i > 0);
    pg_assert(exception_i <= class_file->constant_pool.len);
  }

  pg_array_append(*attributes, attribute, arena);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_inner_classes_attribute(str buf, u8 **current,
                                                cf_class_file_t *class_file,
                                                u32 attribute_len,
                                                arena_t *arena) {
  pg_unused(arena);
  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);
  const u16 entry_size = sizeof(u16) * 4;
  pg_assert(sizeof(table_len) + table_len * entry_size == attribute_len);

  for (u16 i = 0; i < table_len; i++) {
    const u16 inner_class_info_i = buf_read_be_u16(buf, current);
    pg_assert(inner_class_info_i > 0);
    pg_assert(inner_class_info_i <= class_file->constant_pool.len);

    const u16 outer_class_info_i = buf_read_be_u16(buf, current);
    // Could be 0.
    pg_assert(outer_class_info_i <= class_file->constant_pool.len);

    const u16 inner_name_i = buf_read_be_u16(buf, current);
    // Could be 0.
    pg_assert(inner_name_i <= class_file->constant_pool.len);

    const u16 inner_class_access_flags = buf_read_be_u16(buf, current);
    pg_unused(inner_class_access_flags);

    // TODO store.
  }
  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_annotation(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   cf_annotation_t *annotation, arena_t *arena);

static void cf_buf_read_element_value(str buf, u8 **current,
                                      cf_class_file_t *class_file,
                                      cf_element_value_t *element_value,
                                      arena_t *arena) {
  element_value->tag = buf_read_u8(buf, current);
  switch (element_value->tag) {
  case 'B':
  case 'C':
  case 'D':
  case 'I':
  case 'J':
  case 'S':
  case 'Z':
  case 's':
    element_value->v.const_value_index = buf_read_be_u16(buf, current);
    break;

  case 'e':
    element_value->v.enum_const_value.type_name_index =
        buf_read_be_u16(buf, current);
    element_value->v.enum_const_value.const_name_index =
        buf_read_be_u16(buf, current);
    break;

  case 'c':
    element_value->v.class_info_index = buf_read_be_u16(buf, current);
    break;

  case '@': {
    element_value->v.annotation_value = arena_alloc(
        arena, sizeof(cf_annotation_t), _Alignof(cf_annotation_t), 1);

    cf_buf_read_annotation(buf, current, class_file,
                           element_value->v.annotation_value, arena);

    break;
  }

  case '[': {
    const u16 table_len = buf_read_be_u16(buf, current);
    pg_array_init_reserve(element_value->v.array_value, table_len, arena);

    for (u64 i = 0; i < table_len; i++) {
      cf_element_value_t element_value_child = {0};
      cf_buf_read_element_value(buf, current, class_file, &element_value_child,
                                arena);

      pg_array_append(element_value->v.array_value, element_value_child, arena);
    }
    break;
  }

  default:
    pg_assert(0 && "Unexpected value");
  }
}

static void cf_buf_read_annotation(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   cf_annotation_t *annotation,
                                   arena_t *arena) {

  annotation->type_index = buf_read_be_u16(buf, current);
  const u16 num_element_value_pairs = buf_read_be_u16(buf, current);

  pg_array_init_reserve(annotation->element_value_pairs,
                        num_element_value_pairs, arena);

  for (u64 i = 0; i < num_element_value_pairs; i++) {
    cf_element_value_pair_t element_value_pair = {
        .element_name_index = buf_read_be_u16(buf, current),
    };
    cf_buf_read_element_value(buf, current, class_file,
                              &element_value_pair.element_value, arena);

    pg_array_append(annotation->element_value_pairs, element_value_pair, arena);
  }
}

static void cf_buf_read_runtime_invisible_annotations_attribute(
    str buf, u8 **current, cf_class_file_t *class_file, u32 attribute_len,
    cf_attribute_t **attributes, arena_t *arena) {

  const u8 *const current_start = *current;

  const u16 table_len = buf_read_be_u16(buf, current);

  cf_attribute_t attribute = {
      .kind = ATTRIBUTE_KIND_RUNTIME_INVISIBLE_ANNOTATIONS,
  };
  pg_array_init_reserve(attribute.v.runtime_invisible_annotations, table_len,
                        arena);

  for (u64 i = 0; i < table_len; i++) {
    cf_annotation_t annotation = {0};
    cf_buf_read_annotation(buf, current, class_file, &annotation, arena);
    pg_array_append(attribute.v.runtime_invisible_annotations, annotation,
                    arena);
  }
  pg_array_append(*attributes, attribute, arena);

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == attribute_len);
}

static void cf_buf_read_attribute(str buf, u8 **current,
                                  cf_class_file_t *class_file,
                                  cf_attribute_t **attributes, arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(attributes != NULL);
  pg_assert(arena != NULL);

  const u16 name_i = buf_read_be_u16(buf, current);
  pg_assert(name_i > 0);
  const u32 size = buf_read_be_u32(buf, current);
  pg_assert(*current + size <= buf.data + buf.len);

  pg_assert(name_i <= class_file->constant_pool.len);
  str attribute_name =
      cf_constant_array_get_as_string(&class_file->constant_pool, name_i);

  if (str_eq_c(attribute_name, "SourceFile")) {
    pg_assert(2 == size);
    cf_buf_read_sourcefile_attribute(buf, current, class_file, attributes,
                                     arena);
  } else if (str_eq_c(attribute_name, "Code")) {
    cf_buf_read_code_attribute(buf, current, class_file, size, name_i,
                               attributes, arena);
  } else if (str_eq_c(attribute_name, "StackMapTable")) {
    cf_buf_read_stack_map_table_attribute(buf, current, size, name_i,
                                          attributes, arena);
  } else if (str_eq_c(attribute_name, "Exceptions")) {
    cf_buf_read_exceptions_attribute(buf, current, class_file, size, attributes,
                                     arena);
  } else if (str_eq_c(attribute_name, "InnerClasses")) {
    cf_buf_read_inner_classes_attribute(buf, current, class_file, size, arena);
  } else if (str_eq_c(attribute_name, "EnclosingMethod")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "Synthetic")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "Signature")) {
    cf_buf_read_signature_attribute(buf, current, class_file, size, arena);
  } else if (str_eq_c(attribute_name, "SourceDebugExtension")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "LineNumberTable")) {
    cf_buf_read_line_number_table_attribute(buf, current, class_file, size,
                                            attributes, arena);
  } else if (str_eq_c(attribute_name, "LocalVariableTable")) {
    cf_buf_read_local_variable_table_attribute(buf, current, class_file, size,
                                               arena);
  } else if (str_eq_c(attribute_name, "LocalVariableTypeTable")) {
    cf_buf_read_local_variable_type_table_attribute(buf, current, class_file,
                                                    size, arena);
  } else if (str_eq_c(attribute_name, "Deprecated")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "RuntimeVisibleAnnotations")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "RuntimeInvisibleAnnotations")) {
    cf_buf_read_runtime_invisible_annotations_attribute(
        buf, current, class_file, size, attributes, arena);
  } else if (str_eq_c(attribute_name, "RuntimeVisibleParameterAnnotations")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "RuntimeInvisibleParameterAnnotations")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "RuntimeInvisibleAnnotations")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "AnnotationsDefault")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "BootstrapMethods")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "NestMembers")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "NestHost")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "ConstantValue")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "Module")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "ModulePackages")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "ModuleMainClass")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "Record")) {
    *current += size; // TODO
  } else if (str_eq_c(attribute_name, "PermittedSubclasses")) {
    *current += size; // TODO
  } else {
    *current += size; // TODO
  }
}

static void cf_buf_read_attributes(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   cf_attribute_t **attributes,
                                   arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(attributes != NULL);
  pg_assert(arena != NULL);

  const u16 attribute_count = buf_read_be_u16(buf, current);
  pg_array_init_reserve(*attributes, attribute_count, arena);

  for (u64 i = 0; i < attribute_count; i++) {
    cf_buf_read_attribute(buf, current, class_file, attributes, arena);
  }
}

// Returns the number of incoming slots to skip:
// - `1` in the case of CONSTANT_POOL_KIND_LONG or CONSTANT_POOL_KIND_DOUBLE
// - `0` otherwise
static u8 cf_buf_read_constant(str buf, u8 **current,
                               cf_class_file_t *class_file,
                               u16 constant_pool_count, arena_t *arena) {
  u8 kind = buf_read_u8(buf, current);

  if (!(kind == CONSTANT_POOL_KIND_UTF8 || kind == CONSTANT_POOL_KIND_INT ||
        kind == CONSTANT_POOL_KIND_FLOAT || kind == CONSTANT_POOL_KIND_LONG ||
        kind == CONSTANT_POOL_KIND_DOUBLE ||
        kind == CONSTANT_POOL_KIND_CLASS_INFO ||
        kind == CONSTANT_POOL_KIND_STRING ||
        kind == CONSTANT_POOL_KIND_FIELD_REF ||
        kind == CONSTANT_POOL_KIND_METHOD_REF ||
        kind == CONSTANT_POOL_KIND_INTERFACE_METHOD_REF ||
        kind == CONSTANT_POOL_KIND_NAME_AND_TYPE ||
        kind == CONSTANT_POOL_KIND_METHOD_HANDLE ||
        kind == CONSTANT_POOL_KIND_METHOD_TYPE ||
        kind == CONSTANT_POOL_KIND_DYNAMIC ||
        kind == CONSTANT_POOL_KIND_INVOKE_DYNAMIC ||
        kind == CONSTANT_POOL_KIND_MODULE ||
        kind == CONSTANT_POOL_KIND_PACKAGE)) {
    fprintf(stderr, "Unknown constant kind found: offset=%lu kind=%u\n",
            *current - buf.data - 1, kind);
    pg_assert(0);
  }

  switch (kind) {
  case CONSTANT_POOL_KIND_UTF8: { // FIXME: It's actually modified utf8!
    u16 len = buf_read_be_u16(buf, current);

    u8 *const s = *current;
    buf_read_n_u8(buf, len, current);

    cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_UTF8,
                              .v = {.s = str_new(s, len)}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);

    break;
  }
  case CONSTANT_POOL_KIND_INT: {
    const u32 value = buf_read_be_u32(buf, current);
    pg_unused(value);

    const cf_constant_t constant = {.kind = kind, .v = {.number = 0}}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_FLOAT: {
    const u32 value = buf_read_be_u32(buf, current);
    pg_unused(value);

    const cf_constant_t constant = {.kind = kind, .v = {.number = 0}}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_DOUBLE:
  case CONSTANT_POOL_KIND_LONG: {
    const u32 high = buf_read_be_u32(buf, current);
    pg_unused(high);
    const u32 low = buf_read_be_u32(buf, current);
    pg_unused(low);

    const cf_constant_t constant = {.kind = kind, .v = {.number = 0}}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    const cf_constant_t dummy = {0};
    cf_constant_array_push(&class_file->constant_pool, &dummy, arena);
    return 1;
  }
  case CONSTANT_POOL_KIND_CLASS_INFO: {
    const u16 java_class_name_i = buf_read_be_u16(buf, current);
    pg_assert(java_class_name_i > 0);
    pg_assert(java_class_name_i <= constant_pool_count);

    const cf_constant_t constant = {
        .kind = CONSTANT_POOL_KIND_CLASS_INFO,
        .v = {.java_class_name = java_class_name_i}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_STRING: {
    const u16 utf8_i = buf_read_be_u16(buf, current);
    pg_assert(utf8_i > 0);
    pg_assert(utf8_i <= constant_pool_count);

    const cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_STRING,
                                    .v = {.string_utf8_i = utf8_i}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_FIELD_REF: {
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= constant_pool_count);

    const u16 descriptor_i = buf_read_be_u16(buf, current);
    pg_assert(descriptor_i > 0);
    pg_assert(descriptor_i <= constant_pool_count);

    const cf_constant_t constant = {
        .kind = CONSTANT_POOL_KIND_FIELD_REF,
        .v = {.field_ref = {.name = name_i, .descriptor = descriptor_i}}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_METHOD_REF: {
    const u16 class_i = buf_read_be_u16(buf, current);
    pg_assert(class_i > 0);
    pg_assert(class_i <= constant_pool_count);

    const u16 name_and_type_i = buf_read_be_u16(buf, current);
    pg_assert(name_and_type_i > 0);
    pg_assert(name_and_type_i <= constant_pool_count);

    const cf_constant_t constant = {
        .kind = CONSTANT_POOL_KIND_METHOD_REF,
        .v = {.method_ref = {.name_and_type = name_and_type_i,
                             .class = class_i}}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_INTERFACE_METHOD_REF: {
    const u16 class_i = buf_read_be_u16(buf, current);
    pg_assert(class_i > 0);
    pg_assert(class_i <= constant_pool_count);

    const u16 name_and_type_i = buf_read_be_u16(buf, current);
    pg_assert(name_and_type_i > 0);
    pg_assert(name_and_type_i <= constant_pool_count);

    const cf_constant_t constant = {
        .kind = CONSTANT_POOL_KIND_INTERFACE_METHOD_REF,
    }; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_NAME_AND_TYPE: {
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= constant_pool_count);

    const u16 descriptor_i = buf_read_be_u16(buf, current);
    pg_assert(descriptor_i > 0);
    pg_assert(descriptor_i <= constant_pool_count);

    const cf_constant_t constant = {
        .kind = CONSTANT_POOL_KIND_NAME_AND_TYPE,
        .v = {.name_and_type = {.name = name_i, .descriptor = descriptor_i}}};
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_METHOD_HANDLE: {
    const u8 reference_kind = buf_read_u8(buf, current);
    pg_unused(reference_kind);
    const u16 reference_i = buf_read_be_u16(buf, current);
    pg_unused(reference_i);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_METHOD_TYPE: {
    const u16 descriptor = buf_read_be_u16(buf, current);
    pg_assert(descriptor > 0);
    pg_assert(descriptor <= constant_pool_count);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_DYNAMIC: {
    const u16 bootstrap_method_attr_index = buf_read_be_u16(buf, current);
    pg_unused(bootstrap_method_attr_index);

    const u16 name_and_type_index = buf_read_be_u16(buf, current);
    pg_assert(name_and_type_index > 0);
    pg_assert(name_and_type_index <= constant_pool_count);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_INVOKE_DYNAMIC: {
    const u16 bootstrap_method_attr_index = buf_read_be_u16(buf, current);
    pg_unused(bootstrap_method_attr_index);

    const u16 name_and_type_index = buf_read_be_u16(buf, current);
    pg_assert(name_and_type_index > 0);
    pg_assert(name_and_type_index <= constant_pool_count);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_MODULE: {
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= constant_pool_count);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  case CONSTANT_POOL_KIND_PACKAGE: {
    const u16 name_i = buf_read_be_u16(buf, current);
    pg_assert(name_i > 0);
    pg_assert(name_i <= constant_pool_count);

    const cf_constant_t constant = {.kind = kind}; // FIXME
    cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    break;
  }
  default:
    pg_assert(0 && "unreachable");
  }
  return 0;
}

static void cf_buf_read_constants(str buf, u8 **current,
                                  cf_class_file_t *class_file,
                                  u16 constant_pool_count, arena_t *arena) {
  for (u64 i = 0; i < constant_pool_count; i++) {
    pg_assert((u64)(*current - buf.data) < buf.len);
    i += cf_buf_read_constant(buf, current, class_file, constant_pool_count,
                              arena);
    pg_assert((u64)(*current - buf.data) <= buf.len);
  }
  pg_assert(constant_pool_count <= class_file->constant_pool.len);
}

static void cf_buf_read_method(str buf, u8 **current,
                               cf_class_file_t *class_file, arena_t *arena) {
  cf_method_t method = {0};
  method.access_flags = buf_read_be_u16(buf, current);
  method.name = buf_read_be_u16(buf, current);
  pg_assert(
      cf_constant_array_get(&class_file->constant_pool, method.name)->kind ==
      CONSTANT_POOL_KIND_UTF8);

  method.descriptor = buf_read_be_u16(buf, current);
  pg_assert(
      cf_constant_array_get(&class_file->constant_pool, method.name)->kind ==
      CONSTANT_POOL_KIND_UTF8);

  cf_buf_read_attributes(buf, current, class_file, &method.attributes, arena);

  pg_array_append(class_file->methods, method, arena);
}

static void cf_buf_read_methods(str buf, u8 **current,
                                cf_class_file_t *class_file, arena_t *arena) {

  const u16 methods_count = buf_read_be_u16(buf, current);
  pg_array_init_reserve(class_file->methods, methods_count, arena);

  for (u64 i = 0; i < methods_count; i++) {
    cf_buf_read_method(buf, current, class_file, arena);
  }
}

static void cf_buf_read_interfaces(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   arena_t *arena) {

  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);

  const u8 *const current_start = *current;

  const u16 interfaces_count = buf_read_be_u16(buf, current);
  pg_array_init_reserve(class_file->interfaces, interfaces_count, arena);

  for (u16 i = 0; i < interfaces_count; i++) {
    const u16 interface_i = buf_read_be_u16(buf, current);
    pg_assert(interface_i > 0);
    pg_assert(interface_i <= class_file->constant_pool.len);

    pg_array_append(class_file->interfaces, interface_i, arena);
  }

  const u8 *const current_end = *current;
  const u64 read_bytes = current_end - current_start;
  pg_assert(read_bytes == sizeof(u16) + interfaces_count * sizeof(u16));
}

static void cf_buf_read_field(str buf, u8 **current,
                              cf_class_file_t *class_file, arena_t *arena) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);

  cf_field_t field = {0};
  field.access_flags = buf_read_be_u16(buf, current);
  field.name = buf_read_be_u16(buf, current);
  pg_assert(field.name > 0);
  pg_assert(field.name <= class_file->constant_pool.len);

  field.descriptor = buf_read_be_u16(buf, current);
  pg_assert(field.descriptor > 0);
  pg_assert(field.descriptor <= class_file->constant_pool.len);

  cf_buf_read_attributes(buf, current, class_file, &field.attributes, arena);

  pg_array_append(class_file->fields, field, arena);
}

static void cf_buf_read_fields(str buf, u8 **current,
                               cf_class_file_t *class_file, arena_t *arena) {

  const u16 fields_count = buf_read_be_u16(buf, current);
  pg_array_init_reserve(class_file->fields, fields_count, arena);

  for (u16 i = 0; i < fields_count; i++) {
    cf_buf_read_field(buf, current, class_file, arena);
  }
}

static void cf_buf_read_class_file(str buf, u8 **current,
                                   cf_class_file_t *class_file,
                                   arena_t *arena) {

  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);

  // Magic.
  pg_assert(buf_read_u8(buf, current) == 0xca);
  pg_assert(buf_read_u8(buf, current) == 0xfe);
  pg_assert(buf_read_u8(buf, current) == 0xba);
  pg_assert(buf_read_u8(buf, current) == 0xbe);

  class_file->minor_version = buf_read_be_u16(buf, current);
  class_file->major_version = buf_read_be_u16(buf, current);

  const u16 constant_pool_count = buf_read_be_u16(buf, current) - 1;
  pg_assert(constant_pool_count > 0);
  class_file->constant_pool = cf_constant_array_make(
      constant_pool_count * 2,
      arena); // Worst case: only LONG or DOUBLE entries which take 2 slots.
  pg_assert(class_file->constant_pool.values != NULL);
  pg_assert(((u64)class_file->constant_pool.values) % 8 == 0);

  cf_buf_read_constants(buf, current, class_file, constant_pool_count, arena);

  class_file->access_flags = buf_read_be_u16(buf, current);

  class_file->this_class = buf_read_be_u16(buf, current);
  pg_assert(class_file->this_class > 0);
  pg_assert(class_file->this_class <= constant_pool_count);
  const cf_constant_t *const this_class =
      cf_constant_array_get(&class_file->constant_pool, class_file->this_class);
  pg_assert(this_class->kind == CONSTANT_POOL_KIND_CLASS_INFO);
  class_file->class_name = cf_constant_array_get_as_string(
      &class_file->constant_pool, this_class->v.string_utf8_i);

  class_file->super_class = buf_read_be_u16(buf, current);
  pg_assert(class_file->super_class <= constant_pool_count);

  cf_buf_read_interfaces(buf, current, class_file, arena);

  cf_buf_read_fields(buf, current, class_file, arena);

  cf_buf_read_methods(buf, current, class_file, arena);

  cf_buf_read_attributes(buf, current, class_file, &class_file->attributes,
                         arena);

  const u64 remaining = buf.data + buf.len - *current;
  pg_assert(remaining == 0);
}

// Returns the number of incoming slots to skip:
// - `1` in the case of CONSTANT_POOL_KIND_LONG or CONSTANT_POOL_KIND_DOUBLE
// - `0` otherwise
static u8 cf_write_constant(const cf_class_file_t *class_file, FILE *file,
                            const cf_constant_t *constant) {
  pg_assert(class_file != NULL);
  pg_assert(file != NULL);
  pg_assert(constant != NULL);

  fwrite(&constant->kind, sizeof(u8), 1, file);
  switch (constant->kind) {
  case CONSTANT_POOL_KIND_UTF8: {
    str s = constant->v.s;
    file_write_be_u16(file, s.len);
    fwrite(s.data, sizeof(u8), s.len, file);
    break;
  }
  case CONSTANT_POOL_KIND_FLOAT:
  case CONSTANT_POOL_KIND_INT:
    file_write_be_u32(file, constant->v.number);
    break;
  case CONSTANT_POOL_KIND_LONG:
  case CONSTANT_POOL_KIND_DOUBLE:
    file_write_be_u64(file, constant->v.number);
    return 1;
  case CONSTANT_POOL_KIND_CLASS_INFO:
    file_write_be_u16(file, constant->v.java_class_name);
    break;
  case CONSTANT_POOL_KIND_STRING:
    file_write_be_u16(file, constant->v.string_utf8_i);
    break;
  case CONSTANT_POOL_KIND_FIELD_REF: {

    const cf_constant_field_ref_t *const field_ref = &constant->v.field_ref;

    file_write_be_u16(file, field_ref->name);
    file_write_be_u16(file, field_ref->descriptor);
    break;
  }
  case CONSTANT_POOL_KIND_METHOD_REF: {

    const cf_constant_method_ref_t *const method_ref = &constant->v.method_ref;

    file_write_be_u16(file, method_ref->class);
    file_write_be_u16(file, method_ref->name_and_type);
    break;
  }
  case CONSTANT_POOL_KIND_INTERFACE_METHOD_REF:
    pg_assert(0 && "unimplemented");
    break;
  case CONSTANT_POOL_KIND_NAME_AND_TYPE: {

    const cf_constant_name_and_type_t *const name_and_type =
        &constant->v.name_and_type;

    file_write_be_u16(file, name_and_type->name);
    file_write_be_u16(file, name_and_type->descriptor);
    break;
  }
  case CONSTANT_POOL_KIND_INVOKE_DYNAMIC:
    pg_assert(0 && "unimplemented");
    break;
  default:
    pg_assert(0 && "unreachable/unimplemented");
  }
  return 0;
}

static void cf_write_constant_pool(const cf_class_file_t *class_file,
                                   FILE *file) {
  pg_assert(class_file != NULL);
  pg_assert(file != NULL);
  file_write_be_u16(file, class_file->constant_pool.len + 1);

  for (u64 i = 0; i < class_file->constant_pool.len; i++) {
    pg_assert(class_file->constant_pool.values != NULL);
    pg_assert(((u64)class_file->constant_pool.values) % 8 == 0);

    const cf_constant_t *const constant = &class_file->constant_pool.values[i];
    i += cf_write_constant(class_file, file, constant);
  }
}
static void cf_write_interfaces(const cf_class_file_t *class_file, FILE *file) {
  pg_assert(class_file != NULL);
  pg_assert(file != NULL);

  file_write_be_u16(file, class_file->interfaces_count);

  pg_assert(class_file->interfaces_count == 0 && "unimplemented");
}

static void cf_write_fields(const cf_class_file_t *class_file, FILE *file) {
  pg_assert(class_file != NULL);
  pg_assert(file != NULL);

  file_write_be_u16(file, class_file->fields_count);

  pg_assert(class_file->fields_count == 0 && "unimplemented");
}

static u32
cf_compute_verification_info_size(cf_verification_info_t verification_info) {
  pg_assert(verification_info.kind <= 8);

  return verification_info.kind < 7 ? sizeof(u8) : sizeof(u8) + sizeof(u16);
}

static u32 cf_compute_verification_infos_size(
    const cf_stack_map_frame_t *stack_map_frame) {
  pg_assert(stack_map_frame != NULL);

  if (stack_map_frame->kind <= 63) // same_frame
  {
    return 0;
  } else if (64 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 127) { // same_locals_1_stack_item_frame
    const cf_verification_info_t verification_info =
        *pg_array_last(stack_map_frame->frame->stack);
    pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

    return cf_compute_verification_info_size(verification_info);
  } else if (128 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 246) { // reserved
    pg_assert(0 && "unreachable");
  } else if (247 <= stack_map_frame->kind &&
             stack_map_frame->kind <=
                 247) { // same_locals_1_stack_item_frame_extended
    const cf_verification_info_t verification_info =
        *pg_array_last(stack_map_frame->frame->stack);
    pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

    return cf_compute_verification_info_size(verification_info);
  } else if (248 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 250) { // chop_frame
    return 0;
  } else if (251 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 251) { // same_frame_extended
    return 0;
  } else if (252 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 254) { // append_frame
    const u64 count = stack_map_frame->kind - 251;
    u32 size = 0;

    for (u64 i = pg_array_len(stack_map_frame->frame->locals) - count;
         i < pg_array_len(stack_map_frame->frame->locals); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->locals[i].verification_info;

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      size += cf_compute_verification_info_size(verification_info);
    }

    return size;
  } else { // full_frame
    u32 size = 0;
    for (u64 i = 0; i < pg_array_len(stack_map_frame->frame->locals); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->locals[i].verification_info;

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      size += cf_compute_verification_info_size(verification_info);
    }

    for (u64 i = 0; i < pg_array_len(stack_map_frame->frame->stack); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->stack[i];

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      size += cf_compute_verification_info_size(verification_info);
    }

    return size;
  }
  pg_assert(0 && "unreachable");
}

static u32 cf_compute_attribute_size(const cf_attribute_t *attribute) {
  pg_assert(attribute != NULL);

  switch (attribute->kind) {
  case ATTRIBUTE_KIND_SOURCE_FILE:
    return 2;
  case ATTRIBUTE_KIND_CODE: {
    const cf_attribute_code_t *const code = &attribute->v.code;

    u32 size = sizeof(code->max_physical_stack) +
               sizeof(code->max_physical_locals) + sizeof(u32) +
               pg_array_len(code->code) + sizeof(u16) /* exception count */ +
               +pg_array_len(code->exceptions) * sizeof(cf_exception_t) +
               sizeof(u16) // attributes length
        ;

    for (u64 i = 0; i < pg_array_len(code->attributes); i++) {
      const cf_attribute_t *const child_attribute = &code->attributes[i];
      size += sizeof(u16) + sizeof(u32) +
              cf_compute_attribute_size(child_attribute);
    }
    return size;
  }
  case ATTRIBUTE_KIND_LINE_NUMBER_TABLE: {
    return sizeof(u16) /* count */ +
           pg_array_len(attribute->v.line_number_table_entries) *
               sizeof(cf_line_number_table_entry_t);
  }
  case ATTRIBUTE_KIND_STACK_MAP_TABLE: {
    const cf_stack_map_frame_t *const stack_map_frames =
        attribute->v.stack_map_table;
    pg_assert(stack_map_frames != NULL);

    u32 size = sizeof(u16) /* count */;
    for (u16 i = 0; i < pg_array_len(stack_map_frames); i++) {
      const cf_stack_map_frame_t *const stack_map_frame = &stack_map_frames[i];

      if (stack_map_frame->kind <= 63) // same_frame
      {
        size += sizeof(u8);
      } else if (64 <= stack_map_frame->kind &&
                 stack_map_frame->kind <=
                     127) { // same_locals_1_stack_item_frame
        const u32 delta =
            sizeof(u8) + cf_compute_verification_infos_size(stack_map_frame);
        pg_assert(delta >= 2);
        pg_assert(delta <= 4);

        size += delta;
      } else if (128 <= stack_map_frame->kind &&
                 stack_map_frame->kind <= 246) { // reserved
        pg_assert(0 && "unreachable");
      } else if (247 <= stack_map_frame->kind &&
                 stack_map_frame->kind <=
                     247) { // same_locals_1_stack_item_frame_extended
        const u32 delta = sizeof(u8) + sizeof(u16) +
                          cf_compute_verification_infos_size(stack_map_frame);
        pg_assert(delta >= 4);
        pg_assert(delta <= 5);

        size += delta;

      } else if (248 <= stack_map_frame->kind &&
                 stack_map_frame->kind <= 250) { // chop_frame
        size += sizeof(u8) + sizeof(u16);
      } else if (251 <= stack_map_frame->kind &&
                 stack_map_frame->kind <= 251) { // same_frame_extended
        size += sizeof(u8) + sizeof(u16);
      } else if (252 <= stack_map_frame->kind &&
                 stack_map_frame->kind <= 254) { // append_frame
        const u32 delta = sizeof(u8) + sizeof(u16) +
                          cf_compute_verification_infos_size(stack_map_frame);

        pg_assert(delta >= 4);
        pg_assert(delta <= 9);

        size += delta;
      } else { // full_frame
        size += sizeof(u8) + 3 * sizeof(u16) +
                cf_compute_verification_infos_size(stack_map_frame);
      }
    }

    return size;
  }
  case ATTRIBUTE_KIND_EXCEPTIONS:
    return sizeof(u16) /* count */ +
           pg_array_len(attribute->v.exception_index_table) * sizeof(u16);
  case ATTRIBUTE_KIND_RUNTIME_INVISIBLE_ANNOTATIONS: {
    pg_assert(0 && "todo");
  }
  }
  pg_assert(0 && "unreachable");
}

static void cf_write_attributes(FILE *file, const cf_attribute_t *attributes);

static void
cf_write_verification_info(FILE *file,
                           cf_verification_info_t verification_info) {
  pg_assert(file != NULL);
  pg_assert(verification_info.kind <= 8);

  file_write_u8(file, verification_info.kind);

  if (verification_info.kind >= 7) {
    pg_assert(verification_info.extra_data > 0);
    file_write_be_u16(file, verification_info.extra_data);
  }
}

static void cf_write_stack_map_table_attribute(
    FILE *file, const cf_stack_map_frame_t *stack_map_frame) {
  pg_assert(file != NULL);
  pg_assert(stack_map_frame != NULL);

  if (stack_map_frame->kind <= 63) // same_frame
  {
    file_write_u8(file, stack_map_frame->kind);
  } else if (64 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 127) { // same_locals_1_stack_item_frame
    file_write_u8(file, stack_map_frame->kind);
    cf_write_verification_info(file,
                               *pg_array_last(stack_map_frame->frame->stack));
  } else if (128 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 246) { // reserved
    pg_assert(0 && "unreachable");
  } else if (247 <= stack_map_frame->kind &&
             stack_map_frame->kind <=
                 247) { // same_locals_1_stack_item_frame_extended
    pg_assert(0 && "todo");
  } else if (248 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 250) { // chop_frame
    file_write_u8(file, stack_map_frame->kind);
    file_write_be_u16(file, stack_map_frame->offset_delta);
  } else if (251 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 251) { // same_frame_extended
    pg_assert(0 && "todo");
  } else if (252 <= stack_map_frame->kind &&
             stack_map_frame->kind <= 254) { // append_frame
    file_write_u8(file, stack_map_frame->kind);
    file_write_be_u16(file, stack_map_frame->offset_delta);

    const u64 count = stack_map_frame->kind - 251;
    for (u64 i = pg_array_len(stack_map_frame->frame->locals) - count;
         i < pg_array_len(stack_map_frame->frame->locals); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->locals[i].verification_info;

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      cf_write_verification_info(file, verification_info);
    }

  } else { // full_frame

    file_write_u8(file, stack_map_frame->kind);
    file_write_be_u16(file, stack_map_frame->offset_delta);
    file_write_be_u16(file, pg_array_len(stack_map_frame->frame->locals));

    for (u64 i = 0; i < pg_array_len(stack_map_frame->frame->locals); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->locals[i].verification_info;

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      cf_write_verification_info(file, verification_info);
    }

    file_write_be_u16(file, pg_array_len(stack_map_frame->frame->stack));
    for (u64 i = 0; i < pg_array_len(stack_map_frame->frame->stack); i++) {
      const cf_verification_info_t verification_info =
          stack_map_frame->frame->stack[i];

      pg_assert(verification_info.kind != VERIFICATION_INFO_TOP);

      cf_write_verification_info(file, verification_info);
    }
  }
}

static void cf_write_attribute(FILE *file, const cf_attribute_t *attribute) {
  pg_assert(file != NULL);
  pg_assert(attribute != NULL);

  file_write_be_u16(file, attribute->name);

  switch (attribute->kind) {
  case ATTRIBUTE_KIND_SOURCE_FILE: {
    const u32 size = cf_compute_attribute_size(attribute);
    file_write_be_u32(file, size);

    const cf_attribute_source_file_t *const source_file =
        &attribute->v.source_file;
    file_write_be_u16(file, source_file->source_file);

    break;
  }
  case ATTRIBUTE_KIND_CODE: {
    const u32 size = cf_compute_attribute_size(attribute);
    file_write_be_u32(file, size);

    const cf_attribute_code_t *const code = &attribute->v.code;

    file_write_be_u16(file, code->max_physical_stack);

    file_write_be_u16(file, code->max_physical_locals);

    file_write_be_u32(file, pg_array_len(code->code));
    fwrite(code->code, pg_array_len(code->code), sizeof(u8), file);

    file_write_be_u16(file, pg_array_len(code->exceptions));
    pg_assert(pg_array_len(code->exceptions) == 0 && "todo");

    cf_write_attributes(file, code->attributes);

    break;
  }
  case ATTRIBUTE_KIND_LINE_NUMBER_TABLE: {
    const u32 size = cf_compute_attribute_size(attribute);
    file_write_be_u32(file, size);

    for (u16 i = 0; i < pg_array_len(attribute->v.line_number_table_entries);
         i++) {
      cf_line_number_table_entry_t line_number_table =
          attribute->v.line_number_table_entries[i];
      file_write_be_u16(file, line_number_table.start_pc);
      file_write_be_u16(file, line_number_table.line_number);
    }

    break;
  }
  case ATTRIBUTE_KIND_STACK_MAP_TABLE: {
    const u32 size = cf_compute_attribute_size(attribute);
    file_write_be_u32(file, size);

    pg_assert(attribute->v.stack_map_table != NULL);
    const u16 count = pg_array_len(attribute->v.stack_map_table);
    file_write_be_u16(file, count);

    for (u16 i = 0; i < pg_array_len(attribute->v.stack_map_table); i++) {
      const cf_stack_map_frame_t *const stack_map_frame =
          &attribute->v.stack_map_table[i];
      cf_write_stack_map_table_attribute(file, stack_map_frame);
    }
    break;
  }
  default:
    pg_assert(0 && "unreachable");
  }
}

static void cf_write_attributes(FILE *file, const cf_attribute_t *attributes) {
  file_write_be_u16(file, pg_array_len(attributes));

  for (u64 i = 0; i < pg_array_len(attributes); i++) {
    const cf_attribute_t *const attribute = &attributes[i];
    cf_write_attribute(file, attribute);
  }
}

static void cf_write_method(FILE *file, const cf_method_t *method) {
  file_write_be_u16(file, method->access_flags);
  file_write_be_u16(file, method->name);
  file_write_be_u16(file, method->descriptor);

  cf_write_attributes(file, method->attributes);
}

static void cf_write_methods(const cf_class_file_t *class_file, FILE *file) {
  pg_assert(class_file != NULL);
  pg_assert(file != NULL);

  file_write_be_u16(file, pg_array_len(class_file->methods));

  for (u64 i = 0; i < pg_array_len(class_file->methods); i++) {
    const cf_method_t *const method = &class_file->methods[i];
    cf_write_method(file, method);
  }
}

static void cf_write(const cf_class_file_t *class_file, FILE *file) {
  fwrite(&cf_MAGIC_NUMBER, sizeof(cf_MAGIC_NUMBER), 1, file);

  file_write_be_u16(file, class_file->minor_version);
  file_write_be_u16(file, 44 + class_file->major_version);
  cf_write_constant_pool(class_file, file);
  file_write_be_u16(file, class_file->access_flags);
  file_write_be_u16(file, class_file->this_class);
  file_write_be_u16(file, class_file->super_class);

  cf_write_interfaces(class_file, file);
  cf_write_fields(class_file, file);
  cf_write_methods(class_file, file);
  cf_write_attributes(file, class_file->attributes);
  fflush(file);
}

static void cf_init(cf_class_file_t *class_file, arena_t *arena) {
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);

  class_file->constant_pool = cf_constant_array_make(1024, arena);
  pg_array_init_reserve(class_file->interfaces, 64, arena);

  pg_array_init_reserve(class_file->methods, 64, arena);
  pg_array_init_reserve(class_file->fields, 64, arena);

  pg_array_init_reserve(class_file->attributes, 64, arena);
}

static void cf_attribute_code_init(cf_attribute_code_t *code, arena_t *arena) {
  pg_assert(code != NULL);
  pg_assert(arena != NULL);

  pg_array_init_reserve(code->code, 512, arena);
  pg_array_init_reserve(code->attributes, 4, arena);
  pg_array_init_reserve(code->exceptions, 0, arena);
}

static u16 cf_add_constant_string(cf_constant_array_t *constant_pool, str s,
                                  arena_t *arena) {
  pg_assert(constant_pool != NULL);
  pg_assert(!str_is_empty(s));

  const cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_UTF8,
                                  .v = {.s = s}};
  return cf_constant_array_push(constant_pool, &constant, arena);
}

static u16 cf_add_constant_cstring(cf_constant_array_t *constant_pool, char *s,
                                   arena_t *arena) {
  pg_assert(constant_pool != NULL);
  pg_assert(s != NULL);

  const cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_UTF8,
                                  .v = {.s = str_from_c(s)}};
  return cf_constant_array_push(constant_pool, &constant, arena);
}

static u16 cf_add_constant_jstring(cf_constant_array_t *constant_pool,
                                   u16 constant_utf8_i, arena_t *arena) {
  pg_assert(constant_pool != NULL);
  pg_assert(constant_utf8_i > 0);

  const cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_STRING,
                                  .v = {.string_utf8_i = constant_utf8_i}};

  return cf_constant_array_push(constant_pool, &constant, arena);
}

// TODO: sanitize `source_file_name` in case of spaces, etc.
// Transform: `/a/b/foo.kt` to `/a/b/FooKt.class`
static str cf_make_class_file_path_kt(str source_file_name, arena_t *arena) {
  pg_assert(!str_is_empty(source_file_name));
  pg_assert(source_file_name.len > 0);
  pg_assert(arena != NULL);

  str_split_result_t file_extension_split = str_rsplit(source_file_name, '.');
  pg_assert(file_extension_split.found);
  pg_assert(str_eq_c(file_extension_split.right, "kt"));

  str suffix = str_from_c("Kt.class");
  str_builder res = sb_new(file_extension_split.left.len + suffix.len, arena);
  res = sb_append(res, file_extension_split.left, arena);

  // Capitalize
  {
    str_split_result_t last_path_component_split =
        str_rsplit(source_file_name, '/');

    res = sb_capitalize_at(res, last_path_component_split.found_pos);
  }

  res = sb_append(res, suffix, arena);

  return sb_build(res);
}

__attribute__((unused)) static str
cf_get_this_class_name(const cf_class_file_t *class_file) {
  pg_assert(class_file != NULL);

  const cf_constant_t *const this_class =
      cf_constant_array_get(&class_file->constant_pool, class_file->this_class);
  pg_assert(this_class->kind == CONSTANT_POOL_KIND_CLASS_INFO);
  const u16 this_class_i = this_class->v.java_class_name;
  return cf_constant_array_get_as_string(&class_file->constant_pool,
                                         this_class_i);
}

static const cf_attribute_t *
cf_method_find_code_attribute(const cf_method_t *method) {
  for (u64 i = 0; i < pg_array_len(method->attributes); i++) {
    const cf_attribute_t *const attribute = &method->attributes[i];

    if (attribute->kind == ATTRIBUTE_KIND_CODE)
      return attribute;
  }
  return NULL;
}

static const cf_attribute_t *
cf_attribute_by_kind(const cf_attribute_t *attributes,
                     cf_attribute_kind_t kind) {
  for (u64 i = 0; i < pg_array_len(attributes); i++) {
    const cf_attribute_t *const attribute = &attributes[i];

    if (attribute->kind == kind)
      return attribute;
  }
  return NULL;
}

static void
cf_get_source_location_of_function(const cf_class_file_t *class_file,
                                   const cf_method_t *method, str *filename,
                                   u16 *line, arena_t *arena) {
  const cf_attribute_t *const code = cf_method_find_code_attribute(method);
  if (code == NULL)
    return;

  const cf_attribute_t *const source_file =
      cf_attribute_by_kind(code->v.code.attributes, ATTRIBUTE_KIND_SOURCE_FILE);
  if (source_file != NULL) {
    *filename = str_clone(
        cf_constant_array_get_as_string(&class_file->constant_pool,
                                        source_file->v.source_file.source_file),
        arena);
  }

  const cf_attribute_t *const line_number_table = cf_attribute_by_kind(
      code->v.code.attributes, ATTRIBUTE_KIND_LINE_NUMBER_TABLE);
  if (line_number_table != NULL) {
    const cf_line_number_table_entry_t *const entries =
        line_number_table->v.line_number_table_entries;

    if (pg_array_len(entries) > 0)
      *line = entries[0].line_number;
  }
}

// ---------------------------------- Lexer

static u32 lex_get_current_offset(str buf, u8 *const *current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert((uint64_t)(*current - buf.data) <= buf.len);

  return *current - buf.data;
}

static bool lex_is_at_end(str buf, u8 *const *current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert((uint64_t)(*current - buf.data) <= buf.len);

  return lex_get_current_offset(buf, current) == buf.len;
}

static u8 lex_peek(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);

  return lex_is_at_end(buf, current) ? 0 : **current;
}

static u8 lex_peek_next(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);

  u64 remaining = buf.len - lex_get_current_offset(buf, current);
  if (remaining < 1)
    return 0;

  return *(*current + 1);
}

static u8 lex_advance(str buf, u8 **current) {
  pg_assert(!str_is_empty(buf));
  pg_assert(current != NULL);
  pg_assert(*current != NULL);

  const u8 c = **current;

  *current += 1;

  return lex_is_at_end(buf, current) ? 0 : c;
}

static bool lex_match(str buf, u8 **current, u8 c) {
  pg_assert(current != NULL);
  pg_assert(*current != NULL);

  if (lex_peek(buf, current) == c) {
    lex_advance(buf, current);
    return true;
  }
  return false;
}

static void lex_skip_until_incl_1(str buf, u8 **current, u8 c) {
  while (!(lex_is_at_end(buf, current) || lex_peek(buf, current) == c)) {
    lex_advance(buf, current);
  }
}

static void lex_skip_until_incl_2(str buf, u8 **current, u8 c1, u8 c2) {
  while (
      !(lex_is_at_end(buf, current) ||
        (lex_peek(buf, current) == c1 && lex_peek_next(buf, current) == c2))) {
    lex_advance(buf, current);
  }

  if (!lex_is_at_end(buf, current)) {
    pg_assert(lex_peek(buf, current) == c1 &&
              lex_peek_next(buf, current) == c2);
    lex_advance(buf, current);
    lex_advance(buf, current);
  }
}

static bool lex_is_digit(u8 c) { return ('0' <= c && c <= '9'); }

static bool lex_is_identifier_char(u8 c) {
  return ut_char_is_alphabetic(c) || lex_is_digit(c) || c == '_';
}

static u32 lex_number_length(str buf, u32 current_offset) {
  pg_assert(current_offset < buf.len);

  const u32 start_offset = current_offset;
  u8 *current = &buf.data[current_offset];
  pg_assert(lex_is_digit(lex_peek(buf, &current)));

  lex_advance(buf, &current);

  while (!lex_is_at_end(buf, &current)) {
    const u8 c = lex_peek(buf, &current);

    if (!(lex_is_digit(c) || c == '_' || c == 'L'))
      break;
    lex_advance(buf, &current);
  }

  const u32 end_offset_excl = lex_get_current_offset(buf, &current);
  pg_assert(end_offset_excl > start_offset);
  pg_assert(end_offset_excl <= buf.len);

  const u32 len = end_offset_excl - start_offset;
  pg_assert(len >= 1);
  pg_assert(len <= buf.len);
  return len;
}

static u32 lex_string_length(str buf, u32 current_offset) {
  pg_assert(current_offset < buf.len);

  const u32 start_offset = current_offset;
  const u8 *current = &buf.data[current_offset];
  pg_assert(*(current - 1) == '"');

  u8 *end_quote = memchr(current, '"', buf.len - start_offset);
  pg_assert(end_quote != NULL);

  return end_quote - current;
}

// FIXME: probably need to memoize it actually to be able to support:
// - `a.b.c = 1` => `a` has length 1.
// - `var a : kotlin.Int` => `kotlin.Int` has length 9.
static u32 lex_identifier_length(str buf, u32 current_offset) {
  pg_assert(current_offset < buf.len);

  const u32 start_offset = current_offset;
  u8 *current = &buf.data[current_offset];
  pg_assert(ut_char_is_alphabetic(*current));

  lex_advance(buf, &current);

  while (!lex_is_at_end(buf, &current)) {
    const u8 c = lex_peek(buf, &current);

    if (!lex_is_identifier_char(c))
      break;
    lex_advance(buf, &current);
  }

  pg_assert(!lex_is_at_end(buf, &current));
  pg_assert(!lex_is_identifier_char(lex_peek(buf, &current)));

  const u32 end_offset_excl = lex_get_current_offset(buf, &current);
  pg_assert(end_offset_excl > start_offset);
  pg_assert(end_offset_excl <= buf.len);

  const u32 len = end_offset_excl - start_offset;
  pg_assert(len >= 1);
  pg_assert(len <= buf.len);
  return len;
}

static void lex_identifier(lex_lexer_t *lexer, str buf, u8 **current,
                           arena_t *arena) {
  pg_assert(lexer != NULL);
  pg_assert(lexer->tokens != NULL);
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert((uint64_t)(*current - buf.data) <= buf.len);
  pg_assert(ut_char_is_alphabetic(**current));

  const u32 start_offset = lex_get_current_offset(buf, current);
  str identifier = str_new(*current, lex_identifier_length(buf, start_offset));
  *current += identifier.len;
  if (str_eq_c(identifier, "fun")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_FUN,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "true")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_TRUE,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "false")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_FALSE,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "var")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_VAR,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "if")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_IF,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "else")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_ELSE,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "while")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_WHILE,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else if (str_eq_c(identifier, "return")) {
    const lex_token_t token = {
        .kind = TOKEN_KIND_KEYWORD_RETURN,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  } else {
    const lex_token_t token = {
        .kind = TOKEN_KIND_IDENTIFIER,
        .source_offset = start_offset,
    };
    pg_array_append(lexer->tokens, token, arena);
  }
}

static void lex_number(lex_lexer_t *lexer, str buf, u8 **current,
                       arena_t *arena) {
  pg_assert(lexer != NULL);
  pg_assert(lexer->tokens != NULL);
  pg_assert(current != NULL);
  pg_assert(*current != NULL);
  pg_assert((u64)(*current - buf.data) <= buf.len);
  pg_assert(lex_is_digit(lex_peek(buf, current)));

  const u32 start_offset = lex_get_current_offset(buf, current);

  lex_advance(buf, current);

  while (!lex_is_at_end(buf, current)) {
    const u8 c = lex_peek(buf, current);

    if (!(lex_is_digit(c) || c == '_'))
      break;
    lex_advance(buf, current);
  }

  lex_match(buf, current, 'L'); // Long suffix.

  const lex_token_t token = {
      .kind = TOKEN_KIND_NUMBER,
      .source_offset = start_offset,
  };
  pg_array_append(lexer->tokens, token, arena);
}

// FIXME: use str_t
static void lex_lex(lex_lexer_t *lexer, str buf, u8 **current, arena_t *arena) {
  pg_assert(lexer != NULL);
  pg_assert(lexer->line_table != NULL);
  pg_assert(pg_array_len(lexer->line_table) == 0);
  pg_assert(lexer->tokens != NULL);
  pg_assert(current != NULL);
  pg_assert(*current != NULL);

  // Push first line.
  pg_array_append(lexer->line_table, 0, arena);

  // Push first dummy token.
  const lex_token_t dummy_token = {0};
  pg_array_append(lexer->tokens, dummy_token, arena);

  while (!lex_is_at_end(buf, current)) {
    const u8 c = **current;

    switch (c) {
    case '(': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_LEFT_PAREN,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case ')': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_RIGHT_PAREN,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case ',': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_COMMA,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case ':': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_COLON,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '!': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '=')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_NOT_EQUAL,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_NOT,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '{': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_LEFT_BRACE,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '}': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_RIGHT_BRACE,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '+': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_PLUS,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '-': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_MINUS,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '*': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_STAR,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '/': {
      lex_advance(buf, current);
      if (lex_match(buf, current, '/')) { // Line comment.
        lex_skip_until_incl_1(buf, current, '\n');
      } else if (lex_match(buf, current, '*')) { // Delimited comment.
        lex_skip_until_incl_2(buf, current, '*', '/');
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_SLASH,
            .source_offset = lex_get_current_offset(buf, current),
        };
        pg_array_append(lexer->tokens, token, arena);
      }

      break;
    }
    case '%': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_PERCENT,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '.': {
      const lex_token_t token = {
          .kind = TOKEN_KIND_DOT,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_array_append(lexer->tokens, token, arena);
      lex_advance(buf, current);
      break;
    }
    case '&': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '&')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_AMPERSAND_AMPERSAND,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_AMPERSAND,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '|': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '|')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_PIPE_PIPE,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_PIPE,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '=': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '=')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_EQUAL_EQUAL,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_EQUAL,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '<': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '=')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_LE,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_LT,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '>': {
      lex_advance(buf, current);

      if (lex_match(buf, current, '=')) {
        const lex_token_t token = {
            .kind = TOKEN_KIND_GE,
            .source_offset = lex_get_current_offset(buf, current) - 2,
        };
        pg_array_append(lexer->tokens, token, arena);
      } else {
        const lex_token_t token = {
            .kind = TOKEN_KIND_GT,
            .source_offset = lex_get_current_offset(buf, current) - 1,
        };
        pg_array_append(lexer->tokens, token, arena);
      }
      break;
    }
    case '"': {
      lex_advance(buf, current);

      const lex_token_t token = {
          .kind = TOKEN_KIND_STRING_LITERAL,
          .source_offset = lex_get_current_offset(buf, current),
      };
      pg_assert(buf.data[token.source_offset - 1] == '"');

      while (!lex_match(buf, current, '"')) {
        lex_advance(buf, current);
      }
      pg_array_append(lexer->tokens, token, arena);
      break;
    }
    case '\n': {
      lex_advance(buf, current);

      if (!lex_is_at_end(buf, current))
        pg_array_append(lexer->line_table, lex_get_current_offset(buf, current),
                        arena);

      break;
    }
    case ' ': {
      lex_advance(buf, current);
      break;
    }
    default: {
      if (ut_char_is_alphabetic(c)) {
        lex_identifier(lexer, buf, current, arena);
      } else if (lex_is_digit(c)) {
        lex_number(lexer, buf, current, arena);
      } else {
        pg_assert(0 && "unimplemented");
      }
    }
    }
  }
  // Ensure the line table has at least 2 items: line_table=[0]=0,
  // line_table[last]=buf.len, for easier logic later to find token
  // positions.
  pg_array_append(lexer->line_table, buf.len, arena);
}

static u32 lex_find_token_length(const lex_lexer_t *lexer, str buf,
                                 lex_token_t token) {
  pg_assert(lexer != NULL);

  switch (token.kind) {
  case TOKEN_KIND_NONE:
    return 0;
  case TOKEN_KIND_PLUS:
  case TOKEN_KIND_MINUS:
  case TOKEN_KIND_STAR:
  case TOKEN_KIND_SLASH:
  case TOKEN_KIND_PERCENT:
  case TOKEN_KIND_LEFT_PAREN:
  case TOKEN_KIND_RIGHT_PAREN:
  case TOKEN_KIND_LEFT_BRACE:
  case TOKEN_KIND_RIGHT_BRACE:
  case TOKEN_KIND_COMMA:
  case TOKEN_KIND_DOT:
  case TOKEN_KIND_COLON:
  case TOKEN_KIND_NOT:
  case TOKEN_KIND_EQUAL:
  case TOKEN_KIND_LT:
  case TOKEN_KIND_GT:
  case TOKEN_KIND_AMPERSAND:
  case TOKEN_KIND_PIPE:
    return 1;
  case TOKEN_KIND_KEYWORD_IF:
  case TOKEN_KIND_NOT_EQUAL:
  case TOKEN_KIND_LE:
  case TOKEN_KIND_GE:
  case TOKEN_KIND_EQUAL_EQUAL:
  case TOKEN_KIND_AMPERSAND_AMPERSAND:
  case TOKEN_KIND_PIPE_PIPE:
    return 2;
  case TOKEN_KIND_KEYWORD_FUN:
  case TOKEN_KIND_KEYWORD_VAR:
    return 3;
  case TOKEN_KIND_KEYWORD_TRUE:
  case TOKEN_KIND_KEYWORD_ELSE:
    return 4;
  case TOKEN_KIND_KEYWORD_FALSE:
  case TOKEN_KIND_KEYWORD_WHILE:
    return 5;
  case TOKEN_KIND_KEYWORD_RETURN:
    return 6;

  case TOKEN_KIND_NUMBER:
    return lex_number_length(buf, token.source_offset);

  case TOKEN_KIND_IDENTIFIER:
    return lex_identifier_length(buf, token.source_offset);

  case TOKEN_KIND_STRING_LITERAL:
    return lex_string_length(buf, token.source_offset);

  default:
    pg_assert(0 && "unreachable");
  }
  __builtin_unreachable();
}

// ------------------------------ Parser

static u32 par_add_node(parser_t *parser, const par_ast_node_t *node,
                        arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(node != NULL);
  pg_assert(pg_array_len(parser->nodes) < UINT32_MAX);

  pg_array_append(parser->nodes, *node, arena);
  return pg_array_last_index(parser->nodes);
}

static void ut_fwrite_indent(FILE *file, u16 indent) {
  for (u16 i = 0; i < indent; i++) {
    fputc(' ', file);
  }
}

static void par_find_token_position(const parser_t *parser, lex_token_t token,
                                    u32 *line, u32 *column, str *token_string) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(line != NULL);
  pg_assert(column != NULL);
  pg_assert(token_string != NULL);
  pg_assert(pg_array_len(parser->lexer->line_table) > 1);

  *token_string = (str){
      .data = &parser->buf.data[token.source_offset],
      .len = lex_find_token_length(parser->lexer, parser->buf, token),
  };

  for (u32 i = 0; i < pg_array_len(parser->lexer->line_table) - 1; i++) {
    const u32 line_start_offset = parser->lexer->line_table[i];
    const u32 line_end_offset_excl = parser->lexer->line_table[i + 1];
    if (line_start_offset <= token.source_offset &&
        token.source_offset <= line_end_offset_excl) {
      *line = i + 1;
      *column = 1 + token.source_offset - line_start_offset;

      return;
    }
  }
  /* pg_assert(*line > 0); */
  pg_assert(*line <= pg_array_len(parser->lexer->line_table));
  /* pg_assert(*column > 0); */
}

static char *ty_type_kind_string(const ty_type_t *types, u32 type_i) {
  const ty_type_t *const type = &types[type_i];

  switch (type->kind) {
  case TYPE_ANY:
    return "TYPE_ANY";
  case TYPE_UNIT:
    return "TYPE_UNIT";
  case TYPE_BOOLEAN:
    return "TYPE_BOOLEAN";
  case TYPE_BYTE:
    return "TYPE_BYTE";
  case TYPE_CHAR:
    return "TYPE_CHAR";
  case TYPE_SHORT:
    return "TYPE_SHORT";
  case TYPE_INT:
    return "TYPE_INT";
  case TYPE_FLOAT:
    return "TYPE_FLOAT";
  case TYPE_LONG:
    return "TYPE_LONG";
  case TYPE_DOUBLE:
    return "TYPE_DOUBLE";
  case TYPE_STRING:
    return "TYPE_STRING";
  case TYPE_METHOD:
    return "TYPE_METHOD";
  case TYPE_CONSTRUCTOR:
    return "TYPE_CONSTRUCTOR";
  case TYPE_ARRAY:
    return "TYPE_ARRAY";
  case TYPE_INSTANCE:
    return "TYPE_INSTANCE";
  case TYPE_INTEGER_LITERAL:
    return "TYPE_INTEGER_LITERAL";
  }
  pg_assert(0 && "unreachable");
}

static str ty_type_to_human_string(const ty_type_t *types, u32 type_i,
                                   arena_t *arena) {
  const ty_type_t *const type = &types[type_i];

  switch (type->kind) {
  case TYPE_ANY:
    return str_from_c("kotlin.Any");
  case TYPE_BOOLEAN:
    return str_from_c("kotlin.Boolean");
  case TYPE_BYTE:
    return str_from_c("kotlin.Byte");
  case TYPE_CHAR:
    return str_from_c("kotlin.Char");
  case TYPE_SHORT:
    return str_from_c("kotlin.Short");
  case TYPE_INT:
    return str_from_c("kotlin.Int");
  case TYPE_FLOAT:
    return str_from_c("kotlin.Float");
  case TYPE_LONG:
    return str_from_c("kotlin.Long");
  case TYPE_DOUBLE:
    return str_from_c("kotlin.Double");
  case TYPE_STRING:
    return str_from_c("kotlin.String");
  case TYPE_UNIT:
    return str_from_c("kotlin.Unit");
  case TYPE_ARRAY: {
    str_builder res = sb_new(type->this_class_name.len + 256, arena);
    res = sb_append_c(res, "Array<", arena);
    res = sb_append(res,
                    ty_type_to_human_string(types, type->v.array_type_i, arena),
                    arena);
    res = sb_append_char(res, '>', arena);
    return sb_build(res);
  }
  case TYPE_INSTANCE: {
    return type->this_class_name;
  }
  case TYPE_METHOD:
  case TYPE_CONSTRUCTOR: {
    const ty_type_method_t *const method_type = &type->v.method;

    str_builder res = sb_new(128, arena);
    res = sb_append_c(res, "(", arena);
    for (u64 i = 0; i < pg_array_len(method_type->argument_types_i); i++) {
      const u32 argument_type_i = method_type->argument_types_i[i];
      res = sb_append(
          res, ty_type_to_human_string(types, argument_type_i, arena), arena);

      if (i < pg_array_len(method_type->argument_types_i) - 1)
        res = sb_append_c(res, ", ", arena);
    }
    res = sb_append_c(res, ") : ", arena);
    res = sb_append(
        res, ty_type_to_human_string(types, method_type->return_type_i, arena),
        arena);
    return sb_build(res);
  }
  case TYPE_INTEGER_LITERAL: {
    const u32 possible_types = type->v.integer_literal_types;
    str_builder res = sb_new(128, arena);
    res = sb_append_c(res, "Integer literal: ", arena);

    if (possible_types & TYPE_BYTE)
      res = sb_append_c(res, "kotlin.Byte | ", arena);
    if (possible_types & TYPE_SHORT)
      res = sb_append_c(res, "kotlin.Short | ", arena);
    if (possible_types & TYPE_INT)
      res = sb_append_c(res, "kotlin.Int | ", arena);
    if (possible_types & TYPE_LONG)
      res = sb_append_c(res, "kotlin.Long | ", arena);

    return sb_build(res);
  }
  }
  pg_assert(0 && "unreachable");
}

static bool par_is_at_end(const parser_t *parser) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return parser->tokens_i == pg_array_len(parser->lexer->tokens);
}

static lex_token_t par_peek_token(const parser_t *parser) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  if (parser->tokens_i > pg_array_len(parser->lexer->tokens) - 1)
    return (lex_token_t){0};

  return parser->lexer->tokens[parser->tokens_i];
}

static lex_token_t par_peek_next_token(const parser_t *parser) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  if (parser->tokens_i > pg_array_len(parser->lexer->tokens) - 2)
    return (lex_token_t){0};

  return parser->lexer->tokens[parser->tokens_i + 1];
}

static void par_advance_token(parser_t *parser) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  // TODO: should we clamp it to the length?
  parser->tokens_i++;
}

static bool par_match_token(parser_t *parser, lex_token_kind_t kind) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (par_peek_token(parser).kind == kind) {
    par_advance_token(parser);
    return true;
  }
  return false;
}

static void par_error(parser_t *parser, lex_token_t token, const char *error) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  switch (parser->state) {
  case PARSER_STATE_OK: {
    u32 line = 0;
    u32 column = 0;
    str token_string = {0};
    par_find_token_position(parser, token, &line, &column, &token_string);

    fprintf(stderr, "%.*s:%u:%u: around `%.*s`, %s\n",
            (int)parser->lexer->file_path.len, parser->lexer->file_path.data,
            line, column, (int)token_string.len, token_string.data, error);

    parser->state = PARSER_STATE_ERROR;
    break;
  }
  case PARSER_STATE_ERROR:
    parser->state = PARSER_STATE_PANIC;
    break;
  case PARSER_STATE_PANIC:
  case PARSER_STATE_SYNCED:
    break;
  }
}

static void par_expect_token(parser_t *parser, lex_token_kind_t kind,
                             const char *error) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (!par_match_token(parser, kind)) {
    par_error(parser, par_peek_token(parser), error);
  }
}

static const u8 NODE_NUMBER_FLAGS_OVERFLOW = 1 << 0;
static const u8 NODE_NUMBER_FLAGS_FLOAT = 1 << 4;
static const u8 NODE_NUMBER_FLAGS_DOUBLE = 1 << 5;
static const u8 NODE_NUMBER_FLAGS_LONG = 1 << 6;

static u64 par_number(const parser_t *parser, lex_token_t token, u8 *flag) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(flag != NULL);

  pg_assert(token.kind == TOKEN_KIND_NUMBER);

  const u32 start = token.source_offset;
  const u32 length = lex_number_length(parser->buf, start);
  pg_assert(length <= 20);

  u64 number = 0;

  u64 ten_unit = 1;
  for (u32 i = 1; i <= length; i++) {
    pg_assert(i < parser->buf.len);

    const u32 position = start + length - i;
    pg_assert(start <= position);

    const u8 c = parser->buf.data[position];
    pg_assert(lex_is_digit(c) || c == 'L' || c == '_');
    if (c == '_')
      continue;
    // > An integer literal with the long literal mark has type kotlin.Long.
    if (c == 'L') {
      *flag |= NODE_NUMBER_FLAGS_LONG;
      continue;
    }

    const u64 delta = ten_unit * (c - '0');
    i64 number_i64 = (i64)number;
    // > If the value is greater than maximum kotlin.Long value (see built-in
    // integer types), it is an illegal integer literal and should be a
    // compile-time error;
    if (__builtin_add_overflow((i64)number_i64, delta, &number_i64)) {
      *flag |= NODE_NUMBER_FLAGS_OVERFLOW;
      return 0;
    }
    number += delta;
    ten_unit *= 10;
  }

  // > if the value is greater than maximum kotlin.Int value (see built-in
  // integer types), it has type kotlin.Long;
  if (INT32_MAX < number && number <= INT64_MAX)
    *flag |= NODE_NUMBER_FLAGS_LONG;

  // TODO: Float, Double.

  return number;
}

static str par_token_to_str_view(parser_t *parser, u32 token_i) {
  pg_assert(parser != NULL);
  pg_assert(token_i < pg_array_len(parser->lexer->tokens));

  const lex_token_t token = parser->lexer->tokens[token_i];

  return (str){
      .data = &parser->buf.data[token.source_offset],
      .len = lex_find_token_length(parser->lexer, parser->buf, token),
  };
}

static str par_token_range_to_string(parser_t *parser, u32 start_token_incl_i,
                                     u32 end_token_excl_i) {
  pg_assert(parser != NULL);
  pg_assert(start_token_incl_i < pg_array_len(parser->lexer->tokens));
  pg_assert(end_token_excl_i <= pg_array_len(parser->lexer->tokens));

  const u32 start_token_incl_source_offset =
      parser->lexer->tokens[start_token_incl_i].source_offset;
  const u32 end_token_excl_source_offset =
      end_token_excl_i == pg_array_len(parser->lexer->tokens)
          ? parser->buf.len
          : parser->lexer->tokens[end_token_excl_i].source_offset;

  return (str){
      .data = &parser->buf.data[start_token_incl_source_offset],
      .len = end_token_excl_source_offset - start_token_incl_source_offset,
  };
}

static u32 par_parse_expression(parser_t *parser, arena_t *arena);
static u32 par_parse_block(parser_t *parser, arena_t *arena);
static u32 par_parse_postfix_unary_expression(parser_t *parser, arena_t *arena);
static u32 par_parse_statements(parser_t *parser, arena_t *arena);
static u32 par_parse_declaration(parser_t *parser, arena_t *arena);
static u32 par_parse_statement(parser_t *parser, arena_t *arena);
static u32 par_parse_type(parser_t *parser, arena_t *arena);

// block:
//     '{'
//     {NL}
//     statements
//     {NL}
//     '}'
static u32 par_parse_block(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  par_expect_token(parser, TOKEN_KIND_LEFT_BRACE,
                   "Expected a left curly brace to start a block");
  const u32 node_i = par_parse_statements(parser, arena);
  par_expect_token(parser, TOKEN_KIND_RIGHT_BRACE,
                   "Expected a right curly brace to end a block");
  return node_i;
}

// controlStructureBody:
//     block
//     | statement
static u32 par_parse_control_structure_body(parser_t *parser, arena_t *arena) {
  pg_assert(parser);
  pg_assert(arena);

  if (par_peek_token(parser).kind == TOKEN_KIND_LEFT_BRACE)
    return par_parse_block(parser, arena);
  else
    return par_parse_statement(parser, arena);
}

//  'if'
//  {NL}
//  '('
//  {NL}
//  expression
//  {NL}
//  ')'
//  {NL}
//  (controlStructureBody | ([controlStructureBody] {NL} [';'] {NL} 'else'
//  {NL} (controlStructureBody | ';')) | ';')
static u32 par_parse_if_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 main_token_i = parser->tokens_i - 1;

  par_expect_token(parser, TOKEN_KIND_LEFT_PAREN,
                   "expected left parenthesis following if");

  const u32 condition_i = par_parse_expression(parser, arena);

  par_expect_token(parser, TOKEN_KIND_RIGHT_PAREN,
                   "expected right parenthesis following if condition");

  // clang-format off
  //
  //               IF 
  //              /  \
  //    condition     THEN_ELSE )
  //                 /      \
  //             then        else
  //
  // clang-format on

  const par_ast_node_t binary_node = {
      .kind = AST_KIND_THEN_ELSE,
      .main_token_i = parser->tokens_i - 1,
      .lhs = par_parse_control_structure_body(parser, arena), // Then
      .rhs = par_match_token(parser, TOKEN_KIND_KEYWORD_ELSE)
                 ? par_parse_control_structure_body(parser, arena)
                 : 0,
  };
  const u32 binary_node_i = par_add_node(parser, &binary_node, arena);

  const par_ast_node_t if_node = {
      .kind = AST_KIND_IF,
      .main_token_i = main_token_i,
      .lhs = condition_i,
      .rhs = binary_node_i,
  };
  return par_add_node(parser, &if_node, arena);
}

// jumpExpression:
//     ('throw' {NL} expression)
//     | (('return' | RETURN_AT) [expression])
//     | 'continue'
//     | CONTINUE_AT
//     | 'break'
//     | BREAK_AT
static u32 par_parse_jump_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  // TODO: check we are in a function.
  if (par_match_token(parser, TOKEN_KIND_KEYWORD_RETURN)) {
    if (parser->current_function_i == 0) {
      par_error(parser, par_peek_token(parser),
                "code outside of a function body");
      return 0;
    }

    const par_ast_node_t node = {.kind = AST_KIND_RETURN,
                                 .main_token_i = parser->tokens_i - 1,
                                 .lhs = par_parse_expression(parser, arena)};
    return par_add_node(parser, &node, arena);
  }
  return 0;
}

// primaryExpression:
//     parenthesizedExpression
//     | simpleIdentifier
//     | literalConstant
//     | stringLiteral
//     | callableReference
//     | functionLiteral
//     | objectLiteral
//     | collectionLiteral
//     | thisExpression
//     | superExpression
//     | ifExpression
//     | whenExpression
//     | tryExpression
//     | jumpExpression
static u32 par_parse_primary_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (par_match_token(parser, TOKEN_KIND_NUMBER)) {

    const par_ast_node_t node = {
        .kind = AST_KIND_NUMBER,
        .main_token_i = parser->tokens_i - 1,
    };
    return par_add_node(parser, &node, arena);
  } else if (par_match_token(parser, TOKEN_KIND_KEYWORD_FALSE) ||
             par_match_token(parser, TOKEN_KIND_KEYWORD_TRUE)) {
    const lex_token_t token = parser->lexer->tokens[parser->tokens_i - 1];
    const bool is_true = parser->buf.data[token.source_offset] == 't';
    const par_ast_node_t node = {
        .kind = AST_KIND_BOOL,
        .main_token_i = parser->tokens_i - 1,
        .lhs = is_true,
    };
    return par_add_node(parser, &node, arena);
  } else if (par_match_token(parser, TOKEN_KIND_LEFT_PAREN)) {
    const u32 node_i = par_parse_expression(parser, arena);
    // TODO: Locate left parenthesis for the error message.
    par_expect_token(parser, TOKEN_KIND_RIGHT_PAREN,
                     "Expected matching right parenthesis");
    return node_i;
  } else if (par_match_token(parser, TOKEN_KIND_IDENTIFIER)) {
    par_ast_node_t node = {
        .kind = AST_KIND_UNRESOLVED_NAME,
        .main_token_i = parser->tokens_i - 1,
    };
    return par_add_node(parser, &node, arena);
  } else if (par_match_token(parser, TOKEN_KIND_STRING_LITERAL)) {
    const par_ast_node_t node = {.kind = AST_KIND_STRING,
                                 .main_token_i = parser->tokens_i - 1};
    return par_add_node(parser, &node, arena);
  } else if (par_match_token(parser, TOKEN_KIND_KEYWORD_IF)) {
    return par_parse_if_expression(parser, arena);
  } else if (par_peek_token(parser).kind ==
             TOKEN_KIND_KEYWORD_RETURN) { // TODO: More.
    return par_parse_jump_expression(parser, arena);
  }

  return 0;
}

// multiVariableDeclaration:
//     '('
//     {NL}
//     variableDeclaration
//     {{NL} ',' {NL} variableDeclaration}
//     [{NL} ',']
//     {NL}
//     ')'
static u32 par_parse_var_declaration(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  par_expect_token(parser, TOKEN_KIND_IDENTIFIER,
                   "expected variable name (identifier)");
  const u32 name_token_i = parser->tokens_i - 1;

  par_expect_token(parser, TOKEN_KIND_COLON,
                   "expected colon between variable name and type");

  const u32 node_type_i = par_parse_type(parser, arena);

  par_expect_token(parser, TOKEN_KIND_EQUAL, "expected type");

  const par_ast_node_t node = {
      .kind = AST_KIND_VAR_DEFINITION,
      .main_token_i = name_token_i,
      .lhs = node_type_i,
      .rhs = par_parse_expression(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

static bool par_is_lvalue(const parser_t *parser, u32 node_i) {
  pg_assert(parser != NULL);

  const par_ast_node_t *const node = &parser->nodes[node_i];
  switch (node->kind) {
  case AST_KIND_VAR_REFERENCE:
    return true;
    // TODO: more

  default:
    return false;
  }
}

// assignment:
//     ((directlyAssignableExpression '=') | (assignableExpression
//     assignmentAndOperator)) {NL} expression
static u32 par_parse_assignment(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  // We could here try to parse a `directlyAssignableExpression`, and if it
  // fails, or if it succeeds but the next token is *not* `TOKEN_KIND_EQUAL`,
  // backtrack.
  // But that potentially means we are parsing twice every
  // expression and lots of expensive cloning/resetting.
  // Instead, we first parse it as an expression, and if the next
  // token is `TOKEN_KIND_EQUAL`, we check that this expression was indeed a
  // lvalue. Otherwise, we just return this expression, no more work to do.
  u32 lhs_i = par_parse_expression(parser, arena);

  if (par_match_token(parser, TOKEN_KIND_EQUAL)) { // Assignment
    const u32 main_token_i = parser->tokens_i - 1;

    const par_ast_node_t node = {
        .kind = AST_KIND_ASSIGNMENT,
        .lhs = lhs_i,
        .main_token_i = main_token_i,
        .rhs = par_parse_expression(parser, arena),
    };
    return par_add_node(parser, &node, arena);
  }

  return lhs_i;
}

// whileStatement:
//     'while'
//     {NL}
//     '('
//     expression
//     ')'
//     {NL}
//     (controlStructureBody | ';')
static u32 par_parse_while_statement(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  const u32 main_token_i = parser->tokens_i - 1;
  par_expect_token(parser, TOKEN_KIND_LEFT_PAREN,
                   "Expect left parenthesis after the while keyword");

  const u32 condition_i = par_parse_expression(parser, arena);

  par_expect_token(parser, TOKEN_KIND_RIGHT_PAREN,
                   "Expect right parenthesis after the while condition");

  const par_ast_node_t node = {
      .kind = AST_KIND_WHILE_LOOP,
      .main_token_i = main_token_i,
      .lhs = condition_i,
  };
  const u32 node_i = par_add_node(parser, &node, arena);

  parser->nodes[node_i].rhs = par_parse_control_structure_body(parser, arena);

  return node_i;
}

static u32 par_parse_loop_statement(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  if (par_match_token(parser, TOKEN_KIND_KEYWORD_WHILE))
    return par_parse_while_statement(parser, arena);

  return 0;
}

// statement:
//     {label | annotation} (declaration | assignment | loopStatement |
//     expression)
static u32 par_parse_statement(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (parser->current_function_i == 0) {
    par_error(parser, par_peek_token(parser),
              "code outside of a function body");
  }

  u32 node_i = 0;
  if ((node_i = par_parse_loop_statement(parser, arena)) != 0)
    return node_i;

  if ((node_i = par_parse_declaration(parser, arena)) != 0)
    return node_i;

  if ((node_i = par_parse_assignment(parser, arena)) != 0)
    return node_i;

  return par_parse_expression(parser, arena);
}

// navigationSuffix:
//     memberAccessOperator {NL} (simpleIdentifier | parenthesizedExpression |
//     'class')
static u32 par_parse_navigation_suffix(parser_t *parser, u32 *main_token_i,
                                       arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(main_token_i != NULL);
  pg_assert(arena != NULL);

  if (!par_match_token(parser, TOKEN_KIND_DOT))
    return 0;

  *main_token_i = parser->tokens_i - 1;

  if (par_match_token(parser, TOKEN_KIND_IDENTIFIER)) {
    const par_ast_node_t node = {
        .kind = AST_KIND_NAVIGATION,
        .main_token_i = parser->tokens_i - 1,
    };
    return par_add_node(parser, &node, arena);
  }

  if (par_match_token(parser, TOKEN_KIND_LEFT_PAREN)) {
    const u32 node_i = par_parse_expression(parser, arena);
    par_expect_token(parser, TOKEN_KIND_RIGHT_PAREN,
                     "Expected matching right parenthesis after expression");
    return node_i;
  }

  pg_assert(0 && "todo"); // TODO: `class`
}

// valueArguments:
//     '(' {NL} [valueArgument {{NL} ',' {NL} valueArgument} [{NL} ','] {NL}]
//     ')'
static void par_parse_value_arguments(parser_t *parser, u32 **nodes,
                                      arena_t *arena) {

  while (!par_is_at_end(parser)) {
    u32 argument_i = par_parse_expression(parser, arena);
    if (argument_i == 0) {
      const lex_token_t main_token = parser->lexer->tokens[parser->tokens_i];
      par_error(parser, main_token,
                "Expected expression or closing right parenthesis for function "
                "arguments");
      return;
    }
    pg_array_append(*nodes, argument_i, arena);

    if (par_match_token(parser, TOKEN_KIND_COMMA)) {
    }

    if (par_match_token(parser, TOKEN_KIND_RIGHT_PAREN))
      break;
  }

  if (par_is_at_end(parser)) {
    par_error(parser, *pg_array_last(parser->lexer->tokens),
              "Expect matching right parenthesis after function call");
  }
}

// callSuffix:
//     [typeArguments] (([valueArguments] annotatedLambda) | valueArguments)
static u32 par_parse_call_suffix(parser_t *parser, u32 *main_token_i,
                                 arena_t *arena) {
  if (!par_match_token(parser, TOKEN_KIND_LEFT_PAREN))
    return 0;

  *main_token_i = parser->tokens_i - 1;
  par_ast_node_t node = {
      .kind = AST_KIND_CALL,
      .main_token_i = parser->tokens_i - 1,
  };

  // Calling a function with zero arguments.
  if (par_match_token(parser, TOKEN_KIND_RIGHT_PAREN)) {
    pg_array_init_reserve(node.nodes, 0, arena);
    return par_add_node(parser, &node, arena);
  }

  pg_array_init_reserve(node.nodes, 256, arena);
  par_parse_value_arguments(parser, &node.nodes, arena);

  return par_add_node(parser, &node, arena);
}

// postfixUnarySuffix:
//     postfixUnaryOperator
//     | typeArguments
//     | callSuffix
//     | indexingSuffix
//     | navigationSuffix
static u32 par_parse_postfix_unary_suffix(parser_t *parser, u32 *main_token_i,
                                          arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(main_token_i != NULL);
  pg_assert(arena != NULL);

  if (par_peek_token(parser).kind == TOKEN_KIND_DOT)
    return par_parse_navigation_suffix(parser, main_token_i, arena);

  return par_parse_call_suffix(parser, main_token_i, arena);
}

// postfixUnaryExpression:
//     primaryExpression {postfixUnarySuffix}
static u32 par_parse_postfix_unary_expression(parser_t *parser,
                                              arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  u32 lhs_i = par_parse_primary_expression(parser, arena);

  u32 main_token_i = 0;
  // TODO: multiple suffixes.
  const u32 rhs_i =
      par_parse_postfix_unary_suffix(parser, &main_token_i, arena);
  if (rhs_i == 0)
    return lhs_i;

  parser->nodes[rhs_i].lhs = lhs_i;

  return rhs_i;
}

// prefixUnaryExpression:
//     {unaryPrefix} postfixUnaryExpression
static u32 par_parse_prefix_unary_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (par_match_token(parser, TOKEN_KIND_NOT) ||
      par_match_token(parser, TOKEN_KIND_MINUS)) {
    const par_ast_node_t node = {
        .kind = AST_KIND_UNARY,
        .lhs = par_parse_prefix_unary_expression(parser, arena),
        .main_token_i = parser->tokens_i - 1,
    };
    return par_add_node(parser, &node, arena);
  }

  return par_parse_postfix_unary_expression(parser, arena);
}

// asExpression:
//     prefixUnaryExpression {{NL} asOperator {NL} type}
static u32 par_parse_as_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_prefix_unary_expression(parser, arena);
}

// multiplicativeExpression:
//     asExpression {multiplicativeOperator {NL} asExpression}
static u32 par_parse_multiplicative_expression(parser_t *parser,
                                               arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_as_expression(parser, arena);
  if (!(par_match_token(parser, TOKEN_KIND_STAR) ||
        par_match_token(parser, TOKEN_KIND_SLASH) ||
        par_match_token(parser, TOKEN_KIND_PERCENT)))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_multiplicative_expression(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// additiveExpression:
//     multiplicativeExpression {additiveOperator {NL}
//     multiplicativeExpression}
static u32 par_parse_additive_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_multiplicative_expression(parser, arena);
  if (!(par_match_token(parser, TOKEN_KIND_PLUS) ||
        par_match_token(parser, TOKEN_KIND_MINUS)))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_additive_expression(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// rangeExpression:
//     additiveExpression {'..' {NL} additiveExpression}
static u32 par_parse_range_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_additive_expression(parser, arena);
}

// infixFunctionCall:
//     rangeExpression {simpleIdentifier {NL} rangeExpression}
static u32 par_parse_infix_function_call(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_range_expression(parser, arena);
}

// elvisExpression:
//     infixFunctionCall {{NL} elvis {NL} infixFunctionCall}
static u32 par_parse_elvis_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_infix_function_call(parser, arena);
}

// infixOperation:
//     elvisExpression {(inOperator {NL} elvisExpression) | (isOperator {NL}
//     type)}
static u32 par_parse_infix_operation(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_elvis_expression(parser, arena);
}

// genericCallLikeComparison:
//     infixOperation {callSuffix}
static u32 par_parse_generic_call_like_comparison(parser_t *parser,
                                                  arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_infix_operation(parser, arena);
}

// comparison:
//     genericCallLikeComparison {comparisonOperator {NL}
//     genericCallLikeComparison}
static u32 par_parse_comparison(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_generic_call_like_comparison(parser, arena);
  if (!(par_match_token(parser, TOKEN_KIND_LT) ||
        par_match_token(parser, TOKEN_KIND_GT) ||
        par_match_token(parser, TOKEN_KIND_LE) ||
        par_match_token(parser, TOKEN_KIND_GE)))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_comparison(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// equality:
//     comparison {equalityOperator {NL} comparison}
static u32 par_parse_equality(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_comparison(parser, arena);
  if (!(par_match_token(parser, TOKEN_KIND_EQUAL_EQUAL) ||
        par_match_token(parser, TOKEN_KIND_NOT_EQUAL)))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_equality(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// conjunction:
//     equality {{NL} '&&' {NL} equality}
static u32 par_parse_conjunction(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_equality(parser, arena);
  if (!par_match_token(parser, TOKEN_KIND_AMPERSAND_AMPERSAND))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_conjunction(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// disjunction:
//     conjunction {{NL} '||' {NL} conjunction}
static u32 par_parse_disjunction(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  const u32 node_i = par_parse_conjunction(parser, arena);
  if (!par_match_token(parser, TOKEN_KIND_PIPE_PIPE))
    return node_i;

  const par_ast_node_t node = {
      .kind = AST_KIND_BINARY,
      .lhs = node_i,
      .main_token_i = parser->tokens_i - 1,
      .rhs = par_parse_disjunction(parser, arena),
  };
  return par_add_node(parser, &node, arena);
}

// expression:
//     disjunction
static u32 par_parse_expression(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  return par_parse_disjunction(parser, arena);
}

// statements:
//     [statement {semis statement}] [semis]
static u32 par_parse_statements(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (par_peek_token(parser).kind == TOKEN_KIND_RIGHT_BRACE)
    return 0;

  u32 node_i = par_parse_statement(parser, arena);
  if (node_i == 0)
    return node_i;

  par_ast_node_t node = {.kind = AST_KIND_LIST};
  pg_array_init_reserve(node.nodes, 128, arena);
  pg_array_append(node.nodes, node_i, arena);

  while ((node_i = par_parse_statement(parser, arena)) != 0)
    pg_array_append(node.nodes, node_i, arena);

  return par_add_node(parser, &node, arena);
}

// TODO: Parse more complex types.
// type:
//     [typeModifiers] (functionType | parenthesizedType | nullableType |
//     typeReference | definitelyNonNullableType)
static u32 par_parse_type(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(arena != NULL);

  par_expect_token(
      parser, TOKEN_KIND_IDENTIFIER,
      "expected an identifier for the type of the function parameter");

  const par_ast_node_t node = {
      .kind = AST_KIND_TYPE,
      .main_token_i = parser->tokens_i - 1,
  };

  // userType:
  //     simpleUserType {{NL} '.' {NL} simpleUserType}
  while (par_peek_token(parser).kind == TOKEN_KIND_DOT &&
         par_peek_next_token(parser).kind == TOKEN_KIND_IDENTIFIER) {
    par_advance_token(parser);
    par_advance_token(parser);
  }

  return par_add_node(parser, &node, arena);
}

// parameter:
//     simpleIdentifier
//     {NL}
//     ':'
//     {NL}
//     type
static u32 par_parse_parameter(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(arena != NULL);

  par_expect_token(
      parser, TOKEN_KIND_IDENTIFIER,
      "expected an identifier for the name of the function parameter");

  const u32 name_i = parser->tokens_i - 1;

  par_expect_token(
      parser, TOKEN_KIND_COLON,
      "expected a colon between the function parameter name and type");

  const par_ast_node_t node = {
      .kind = AST_KIND_FUNCTION_PARAMETER,
      .main_token_i = name_i,
      .lhs = par_parse_type(parser, arena),
  };
  const u32 node_i = par_add_node(parser, &node, arena);

  return node_i;
}

// functionValueParameter:
//     [parameterModifiers] parameter [{NL} '=' {NL} expression]
static u32 par_parse_function_value_parameter(parser_t *parser,
                                              arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(arena != NULL);

  return par_parse_parameter(parser, arena);
}

// functionValueParameters:
//     '('
//     {NL}
//     [functionValueParameter {{NL} ',' {NL} functionValueParameter} [{NL}
//     ',']] {NL}
//     ')'
static u32 par_parse_function_value_parameters(parser_t *parser,
                                               arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  // No arguments.
  if (par_match_token(parser, TOKEN_KIND_RIGHT_PAREN))
    return 0;

  par_ast_node_t node = {.kind = AST_KIND_LIST};
  pg_array_init_reserve(node.nodes, 128, arena);

  const u32 root_i = par_add_node(parser, &node, arena);
  do {
    pg_array_append(node.nodes,
                    par_parse_function_value_parameter(parser, arena), arena);
  } while (par_match_token(parser, TOKEN_KIND_COMMA));

  par_expect_token(parser, TOKEN_KIND_RIGHT_PAREN,
                   "expected right parenthesis after the arguments");
  return root_i;
}

// functionBody:
//     block
//     | ('=' {NL} expression)
static u32 par_parse_function_body(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  return par_parse_block(parser, arena);
}

// functionDeclaration:
//     [modifiers]
//     'fun'
//     [{NL} typeParameters]
//     [{NL} receiverType {NL} '.']
//     {NL}
//     simpleIdentifier
//     {NL}
//     functionValueParameters
//     [{NL} ':' {NL} type]
//     [{NL} typeConstraints]
//     [{NL} functionBody]
static u32 par_parse_function_declaration(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  if (!par_match_token(parser, TOKEN_KIND_KEYWORD_FUN))
    return 0;

  par_expect_token(parser, TOKEN_KIND_IDENTIFIER,
                   "expected function name (identifier)");
  const u32 start_token = parser->tokens_i - 1;

  par_ast_node_t node = {
      .kind = AST_KIND_FUNCTION_DEFINITION,
      .main_token_i = start_token,
  };

  const u32 fn_i = parser->current_function_i =
      par_add_node(parser, &node, arena);

  par_expect_token(parser, TOKEN_KIND_LEFT_PAREN,
                   "expected left parenthesis before the arguments");

  parser->nodes[parser->current_function_i].lhs =
      par_parse_function_value_parameters(parser, arena);

  if (par_match_token(parser, TOKEN_KIND_COLON)) {
    parser->nodes[parser->current_function_i].extra_data_i =
        par_parse_type(parser, arena);
  }

  parser->nodes[parser->current_function_i].rhs =
      par_parse_function_body(parser, arena);

  parser->current_function_i = 0;

  return fn_i;
}

static void par_sync_if_panicked(parser_t *parser) {
  pg_assert(parser != NULL);

  if (parser->state != PARSER_STATE_PANIC)
    return; // Nothing to do.

  parser->state = PARSER_STATE_SYNCED;

  while (!par_is_at_end(parser)) {
    // TODO: sync at newlines?

    switch (par_peek_token(parser).kind) {
    case TOKEN_KIND_KEYWORD_FUN:
      return;
    default:; // Do nothing.
    }

    par_advance_token(parser);
  }
}

// propertyDeclaration:
//     [modifiers]
//     ('val' | 'var')
//     [{NL} typeParameters]
//     [{NL} receiverType {NL} '.']
//     ({NL} (multiVariableDeclaration | variableDeclaration))
//     [{NL} typeConstraints]
//     [{NL} (('=' {NL} expression) | propertyDelegate)]
//     [(NL {NL}) ';']
//     {NL}
//     (([getter] [{NL} [semi] setter]) | ([setter] [{NL} [semi] getter]))
static u32 par_parse_property_declaration(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(arena != NULL);

  if (par_match_token(parser, TOKEN_KIND_KEYWORD_VAR))
    return par_parse_var_declaration(parser, arena);

  return 0;
}

// declaration:
//     classDeclaration
//     | objectDeclaration
//     | functionDeclaration
//     | propertyDeclaration
//     | typeAlias
static u32 par_parse_declaration(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->nodes != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));

  u32 new_node_i = 0;
  if ((new_node_i = par_parse_function_declaration(parser, arena)) != 0)
    return new_node_i;

  if ((new_node_i = par_parse_property_declaration(parser, arena)) != 0)
    return new_node_i;

  par_sync_if_panicked(parser);

  return new_node_i;
}

// topLevelObject: declaration [semis]
static u32 par_parse_top_level_object(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(pg_array_len(parser->lexer->tokens) >= 1);

  return par_parse_declaration(parser, arena);
}

// kotlinFile:
//     [shebangLine]
//     {NL}
//     {fileAnnotation}
//     packageHeader
//     importList
//     {topLevelObject}
//     EOF
static u32 par_parse_kotlin_file(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(pg_array_len(parser->lexer->tokens) >= 1);

  par_ast_node_t node = {.kind = AST_KIND_LIST};
  pg_array_init_reserve(node.nodes, 512, arena);

  // TODO: package, import, etc.

  u32 node_i = 0;
  while ((node_i = par_parse_top_level_object(parser, arena)) != 0) {
    pg_array_append(node.nodes, node_i, arena);
  }

  if (parser->tokens_i != pg_array_len(parser->lexer->tokens)) {
    par_error(parser, parser->lexer->tokens[parser->tokens_i],
              "Unexpected trailing code");
  }

  return par_add_node(parser, &node, arena);
}

static u32 par_parse(parser_t *parser, arena_t *arena) {
  pg_assert(parser != NULL);
  pg_assert(parser->lexer != NULL);
  pg_assert(parser->lexer->tokens != NULL);
  pg_assert(parser->tokens_i <= pg_array_len(parser->lexer->tokens));
  pg_assert(pg_array_len(parser->lexer->tokens) >= 1);

  pg_array_init_reserve(parser->nodes, pg_array_len(parser->lexer->tokens) * 2,
                        arena);

  parser->tokens_i = 1; // Skip the dummy token.

  const par_ast_node_t dummy_node = {0};
  par_add_node(parser, &dummy_node, arena);

  const u32 root_i = par_parse_kotlin_file(parser, arena);

  return root_i;
}

// --------------------------------- Typing

// TODO: Caching?
static u32 resolver_add_type(resolver_t *resolver, ty_type_t *new_type,
                             arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->types != NULL);
  pg_assert(new_type != NULL);

  if (new_type->kind == TYPE_INSTANCE) { // Try to lower to a know type.
    if (str_eq_c(new_type->this_class_name, "java/lang/Boolean"))
      new_type->kind = TYPE_BOOLEAN;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Char"))
      new_type->kind = TYPE_CHAR;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Byte"))
      new_type->kind = TYPE_BYTE;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Short"))
      new_type->kind = TYPE_SHORT;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Integer"))
      new_type->kind = TYPE_INT;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Float"))
      new_type->kind = TYPE_FLOAT;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Long"))
      new_type->kind = TYPE_LONG;
    else if (str_eq_c(new_type->this_class_name, "java/lang/Double"))
      new_type->kind = TYPE_DOUBLE;
    else if (str_eq_c(new_type->this_class_name, "java/lang/String"))
      new_type->kind = TYPE_STRING;
  }

  pg_array_append(resolver->types, *new_type, arena);
  return pg_array_last_index(resolver->types);
}

static str resolver_function_to_human_string(const resolver_t *resolver,
                                             u32 function_i, arena_t *arena);

static bool
cf_method_has_inline_only_annotation(const cf_class_file_t *class_file,
                                     const cf_method_t *method) {

  for (u64 i = 0; i < pg_array_len(method->attributes); i++) {
    const cf_attribute_t *const attribute = &method->attributes[i];
    if (attribute->kind != ATTRIBUTE_KIND_RUNTIME_INVISIBLE_ANNOTATIONS)
      continue;

    for (u64 j = 0;
         j < pg_array_len(attribute->v.runtime_invisible_annotations); j++) {
      const cf_annotation_t *const annotation =
          &attribute->v.runtime_invisible_annotations[j];

      str descriptor = cf_constant_array_get_as_string(
          &class_file->constant_pool, annotation->type_index);

      if (str_eq_c(descriptor, "Lkotlin/internal/InlineOnly;"))
        return true;
    }
  }
  return false;
}

static void resolver_load_methods_from_class_file(
    resolver_t *resolver, u32 this_class_type_i,
    const cf_class_file_t *class_file, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);

  bool has_at_least_one_inline_only_method = false;
  const u64 initial_types_count = pg_array_len(resolver->types);
  const ty_type_t *const this_class_type = &resolver->types[this_class_type_i];

  for (u64 i = 0; i < pg_array_len(class_file->methods); i++) {
    const cf_method_t *const method = &class_file->methods[i];
    str descriptor = cf_constant_array_get_as_string(&class_file->constant_pool,
                                                     method->descriptor);
    str name = cf_constant_array_get_as_string(&class_file->constant_pool,
                                               method->name);

    ty_type_t type = {
        .this_class_name = this_class_type->this_class_name,
        .package_name = this_class_type->package_name,
    };
    cf_parse_descriptor(resolver, descriptor, &type, arena);
    pg_assert(type.kind == TYPE_METHOD);

    if (str_eq_c(name, CONSTRUCTOR_JVM_NAME)) {
      type.kind = TYPE_CONSTRUCTOR;
      type.v.method.name = type.this_class_name;
    } else {
      type.v.method.name = name;
    }

    type.v.method.access_flags = method->access_flags;
    type.v.method.this_class_type_i = this_class_type_i;
    cf_get_source_location_of_function(class_file, method,
                                       &type.v.method.source_file_name,
                                       &type.v.method.source_line, arena);

    if (cf_method_has_inline_only_annotation(class_file, method)) {
      // Do as if the method was public, not private.
      type.v.method.access_flags |= ACCESS_FLAGS_PUBLIC;
      type.v.method.access_flags &= (~1UL << ACCESS_FLAGS_PRIVATE);
      type.flags |= TYPE_FLAG_INLINE_ONLY;

      has_at_least_one_inline_only_method = true;

      // Clone code.
      // TODO: Clone exceptions, stack map frames, etc?
      for (u64 i = 0; i < pg_array_len(method->attributes); i++) {
        const cf_attribute_t *const attribute = &method->attributes[i];
        if (attribute->kind == ATTRIBUTE_KIND_CODE) {
          pg_array_clone(type.v.method.code, attribute->v.code.code, arena);
          break;
        }
      }
      pg_assert(pg_array_len(type.v.method.code) > 0);
    }

    const u32 type_i = resolver_add_type(resolver, &type, arena);

    if (cli_log_verbose) {
      arena_t tmp_arena = *arena;
      str human_type =
          resolver_function_to_human_string(resolver, type_i, &tmp_arena);
      LOG("Loaded %s: access_flags=%u type=%.*s",
          ty_type_kind_string(resolver->types, type_i), method->access_flags,
          (int)human_type.len, human_type.data);
    }
  }

  if (has_at_least_one_inline_only_method) {
    // Need each inline-only method to point to a clone of the constant pool to
    // be able to fix-up the referred to constants.
    cf_constant_array_t *constant_pool_clone =
        cf_constant_array_clone(&class_file->constant_pool, arena);
    for (u64 i = initial_types_count; i < pg_array_len(resolver->types); i++) {
      ty_type_t *const type = &resolver->types[i];
      if (type->kind == TYPE_METHOD && (type->flags & TYPE_FLAG_INLINE_ONLY))
        type->v.method.constant_pool = constant_pool_clone;
    }
  }
}

static bool cf_buf_read_jar_file(resolver_t *resolver, str content, str path,
                                 arena_t scratch_arena, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(arena != NULL);

  u8 *current = content.data;
  const u64 central_directory_end_size = 22;
  pg_assert(content.len >= 4 + central_directory_end_size);
  pg_assert(buf_read_u8(content, &current) == 0x50);
  pg_assert(buf_read_u8(content, &current) == 0x4b);
  pg_assert(buf_read_u8(content, &current) == 0x03);
  pg_assert(buf_read_u8(content, &current) == 0x04);

  // Assume first no trailing comment.
  u8 *cdre = content.data + content.len - central_directory_end_size;
  if (buf_read_le_u32(content, &cdre) != 0x06054b50) {
    // Need to scan backwards in the presence of a trailing comment to find
    // the magic number.
    cdre -= sizeof(u32);
    while (--cdre > content.data &&
           buf_read_le_u32(content, &cdre) != 0x06054b50) {
      cdre -= sizeof(u32);
    }
    pg_assert(cdre > content.data);
  }

  // disk number
  const u16 disk_number = buf_read_le_u16(content, &cdre);
  pg_unused(disk_number);

  // disk start
  const u16 disk_start = buf_read_le_u16(content, &cdre);

  // records count on this disk
  const u16 disk_records_count = buf_read_le_u16(content, &cdre);
  pg_unused(disk_records_count);

  const u16 records_count = buf_read_le_u16(content, &cdre);

  const u32 central_directory_size = buf_read_le_u32(content, &cdre);
  pg_unused(central_directory_size);

  const u32 central_directory_offset = buf_read_le_u32(content, &cdre);

  pg_assert(central_directory_offset < content.len);

  // Sign of zip64.
  pg_assert(central_directory_offset != (u32)-1);

  u8 *cdfh = content.data + central_directory_offset;
  for (u64 i = 0; i < records_count; i++) {
    pg_assert(buf_read_u8(content, &cdfh) == 0x50);
    pg_assert(buf_read_u8(content, &cdfh) == 0x4b);
    pg_assert(buf_read_u8(content, &cdfh) == 0x01);
    pg_assert(buf_read_u8(content, &cdfh) == 0x02);

    // version made by
    buf_read_le_u16(content, &cdfh);

    // version needed to extract
    buf_read_le_u16(content, &cdfh);

    // general purpose bit flag
    buf_read_le_u16(content, &cdfh);

    const u16 compression_method = buf_read_le_u16(content, &cdfh);
    pg_assert(compression_method == 0 ||
              compression_method == 8); // No compression or DEFLATE.

    // file last modification time
    buf_read_le_u16(content, &cdfh);

    // file last modification date
    buf_read_le_u16(content, &cdfh);

    // crc-32 of uncompressed data
    buf_read_le_u32(content, &cdfh);

    // compressed size
    const u32 compressed_size_according_to_directory_entry =
        buf_read_le_u32(content, &cdfh);

    // uncompressed size
    const u32 uncompressed_size_according_to_directory_entry =
        buf_read_le_u32(content, &cdfh);

    const u16 file_name_length = buf_read_le_u16(content, &cdfh);

    const u16 extra_field_length = buf_read_le_u16(content, &cdfh);

    const u16 file_comment_length = buf_read_le_u16(content, &cdfh);

    // disk number where file starts
    buf_read_le_u16(content, &cdfh);

    // internal file attributes
    buf_read_le_u16(content, &cdfh);

    // external file attributes
    buf_read_le_u32(content, &cdfh);

    const u32 local_file_header_offset = buf_read_le_u32(content, &cdfh);

    // file name
    buf_read_n_u8(content, file_name_length, &cdfh);

    // extra field
    buf_read_n_u8(content, extra_field_length, &cdfh);

    // file comment
    buf_read_n_u8(content, file_comment_length, &cdfh);

    // Read file header.
    {
      u8 *local_file_header =
          content.data + disk_start + local_file_header_offset;
      pg_assert(buf_read_u8(content, &local_file_header) == 0x50);
      pg_assert(buf_read_u8(content, &local_file_header) == 0x4b);
      pg_assert(buf_read_u8(content, &local_file_header) == 0x03);
      pg_assert(buf_read_u8(content, &local_file_header) == 0x04);

      // version to extract
      buf_read_le_u16(content, &local_file_header);

      // general purpose bit flag
      buf_read_le_u16(content, &local_file_header);

      const u16 compression_method =
          buf_read_le_u16(content, &local_file_header);
      pg_assert(compression_method == 0 ||
                compression_method == 8); // No compression or DEFLATE.

      // file last modification time
      buf_read_le_u16(content, &local_file_header);

      // file last modification date
      buf_read_le_u16(content, &local_file_header);

      // crc-32 of uncompressed data
      buf_read_le_u32(content, &local_file_header);

      // compressed size
      buf_read_le_u32(content, &local_file_header);

      // uncompressed size
      buf_read_le_u32(content, &local_file_header);

      const u16 file_name_length = buf_read_le_u16(content, &local_file_header);

      const u16 extra_field_length =
          buf_read_le_u16(content, &local_file_header);

      str file_name = {.data = local_file_header, .len = file_name_length};
      buf_read_n_u8(content, file_name_length, &local_file_header);

      buf_read_n_u8(content, extra_field_length, &local_file_header);

      cf_class_file_t class_file = {
          .class_file_path = file_name,
          .archive_file_path = path,
      };
      // TODO: Read Manifest file?
      if (uncompressed_size_according_to_directory_entry > 0 &&
          compression_method == 0 && str_ends_with_c(file_name, ".class")) {

        cf_buf_read_class_file(
            str_new(local_file_header,
                    uncompressed_size_according_to_directory_entry),
            &local_file_header, &class_file, &scratch_arena);

        ty_type_t type = {.kind = TYPE_INSTANCE};
        type_init_package_and_name(class_file.class_name, &type.package_name,
                                   &type.this_class_name, arena);

        const u32 this_class_type_i = resolver_add_type(resolver, &type, arena);

        if (class_file.super_class != 0) {
          const cf_constant_t *const constant_super = cf_constant_array_get(
              &class_file.constant_pool, class_file.super_class);

          pg_assert(constant_super->kind == CONSTANT_POOL_KIND_CLASS_INFO);
        }

        resolver_load_methods_from_class_file(resolver, this_class_type_i,
                                              &class_file, arena);

        LOG("Loaded class_file_path=%.*s  archive_file_path=%.*s "
            "kind=uncompressed package_name=%.*s class_name=%.*s",
            (int)class_file.class_file_path.len,
            class_file.class_file_path.data,
            (int)class_file.archive_file_path.len,
            class_file.archive_file_path.data, (int)type.package_name.len,
            type.package_name.data, (int)type.this_class_name.len,
            type.this_class_name.data);

      } else if (compressed_size_according_to_directory_entry > 0 &&
                 compression_method == 8 && str_eq_c(file_name, ".class")) {
        str dst = sb_build(sb_new(
            uncompressed_size_according_to_directory_entry, &scratch_arena));

        z_stream stream = {
            .next_in = (u8 *)local_file_header,
            .avail_in = compressed_size_according_to_directory_entry,
            .next_out = dst.data,
            .avail_out = dst.len,
        };

        // `inflateInit2` is required instead of `inflateInit` because this is a
        // raw compressed stream and not a zlib compressed stream which contains
        // a header.
        int res = inflateInit2(&stream, -8);
        pg_assert(res == Z_OK);

        res = inflate(&stream, Z_SYNC_FLUSH);
        pg_assert(res == Z_STREAM_END);

        u8 *dst_current = dst.data;
        cf_buf_read_class_file(dst, &dst_current, &class_file, &scratch_arena);

        ty_type_t type = {.kind = TYPE_INSTANCE};
        type_init_package_and_name(class_file.class_name, &type.package_name,
                                   &type.this_class_name, arena);

        const u32 this_class_type_i = resolver_add_type(resolver, &type, arena);

        if (class_file.super_class != 0) {
          const cf_constant_t *const constant_super = cf_constant_array_get(
              &class_file.constant_pool, class_file.super_class);

          pg_assert(constant_super->kind == CONSTANT_POOL_KIND_CLASS_INFO);
          str super_class_name = cf_constant_array_get_as_string(
              &class_file.constant_pool, constant_super->v.string_utf8_i);

          if (!str_eq_c(super_class_name, "java/lang/Object")) {
            type.super_class_name = super_class_name;
          }
        }

        resolver_load_methods_from_class_file(resolver, this_class_type_i,
                                              &class_file, arena);

        LOG("Loaded class_file_path=%.*s  archive_file_path=%.*s "
            "kind=compressed package_name=%.*s class_name=%.*s",
            (int)class_file.class_file_path.len,
            class_file.class_file_path.data,
            (int)class_file.archive_file_path.len,
            class_file.archive_file_path.data, (int)type.package_name.len,
            type.package_name.data, (int)type.this_class_name.len,
            type.this_class_name.data);

        inflateEnd(&stream);
      }
    }
  }
  return false;
}
static bool cf_read_jmod_file(resolver_t *resolver, str path,
                              arena_t scratch_arena, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(arena != NULL);

  ut_read_result_t read_res =
      // IMPORTANT: We store the content of JMOD files in the *scratch* arena,
      // not the *main* arena. That's because most of the stuff in there is
      // irrelevant. We pick afterwards just the few bits we want to retain and
      // clone them into the main arena.
      ut_read_all_from_file_path(path, scratch_arena, &scratch_arena);
  if (read_res.error) {
    fprintf(stderr, "Failed to open the file %.*s: %s\n", (int)path.len,
            path.data, strerror(read_res.error));
    return false;
  }

  str content = read_res.content;
  // Check magic number.
  {
    u8 *current = content.data;
    pg_assert(buf_read_u8(content, &current) == 'J');
    pg_assert(buf_read_u8(content, &current) == 'M');
    pg_assert(buf_read_u8(content, &current) == 1);
    pg_assert(buf_read_u8(content, &current) == 0);
  }

  content = str_advance(content, 4);
  return cf_buf_read_jar_file(resolver, content, path, scratch_arena, arena);
}

static bool cf_read_jar_file(resolver_t *resolver, str path,
                             arena_t scratch_arena, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(arena != NULL);

  ut_read_result_t read_res =
      ut_read_all_from_file_path(path, scratch_arena, arena);
  if (read_res.error) {
    fprintf(stderr, "Failed to open the file %.*s: %s\n", (int)path.len,
            path.data, strerror(read_res.error));
    return false;
  }

  return cf_buf_read_jar_file(resolver, read_res.content, path, scratch_arena,
                              arena);
}

static bool resolver_is_package_imported(const resolver_t *resolver,
                                         str package_name) {
  for (u64 i = 0; i < pg_array_len(resolver->imported_package_names); i++) {
    if (str_eq(resolver->imported_package_names[i], package_name))
      return true;
  }

  return false;
}

static void resolver_collect_callables_with_name(const resolver_t *resolver,
                                                 str function_name,
                                                 u32 **candidate_functions_i,
                                                 arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->types != NULL);
  pg_assert(candidate_functions_i != NULL);
  pg_assert(*candidate_functions_i != NULL);

  for (u64 i = 0; i < pg_array_len(resolver->types); i++) {
    const ty_type_t *const type = &resolver->types[i];
    if (type->kind == TYPE_METHOD || type->kind == TYPE_CONSTRUCTOR) {
      const ty_type_method_t *const method = &type->v.method;

      if ((method->access_flags & ACCESS_FLAGS_STATIC) == 0)
        continue;

      if (!str_eq(method->name, function_name))
        continue;

      // TODO: Should loaded but not yet imported types reside in a different
      // array to avoid thrashing?
      if (!resolver_is_package_imported(resolver, type->package_name))
        continue;

      pg_array_append(*candidate_functions_i, i, arena);
    }
  }

  // TODO: Collect callable fields as well.
}

// TODO: Add to string: file:line
static str resolver_function_to_human_string(const resolver_t *resolver,
                                             u32 type_i, arena_t *arena) {

  const ty_type_t *const type = &resolver->types[type_i];

  if (!(type->kind == TYPE_METHOD || type->kind == TYPE_CONSTRUCTOR))
    return ty_type_to_human_string(resolver->types, type_i, arena);

  const ty_type_method_t *const method = &type->v.method;

  const ty_type_t *const this_class_type =
      &resolver->types[method->this_class_type_i];

  str_builder res = sb_new(
      method->name.len + this_class_type->this_class_name.len + 1024, arena);

  if (method->access_flags & ACCESS_FLAGS_PRIVATE)
    res = sb_append_c(res, "private ", arena);

  res = sb_append_c(res, "fun ", arena);

  if (!str_is_empty(type->package_name)) {
    res = sb_append(res, type->package_name, arena);
    res = sb_append_c(res, ".", arena);
  }

  if ((method->access_flags & ACCESS_FLAGS_STATIC) == 0) {
    res = sb_append(res, this_class_type->this_class_name, arena);
    res = sb_append_c(res, ".", arena);
  }

  res = sb_append(res, method->name, arena);
  res = sb_append_c(res, "(", arena);

  const u8 argument_count = pg_array_len(method->argument_types_i);
  for (u8 i = 0; i < argument_count; i++) {
    str argument_type_string = ty_type_to_human_string(
        resolver->types, method->argument_types_i[i], arena);

    res = sb_append(res, argument_type_string, arena);

    if (i < argument_count - 1)
      res = sb_append_c(res, ", ", arena);
  }

  res = sb_append_c(res, ") : ", arena);
  str return_type_string =
      ty_type_to_human_string(resolver->types, method->return_type_i, arena);
  res = sb_append(res, return_type_string, arena);

  res = sb_append_c(res, " from ", arena);

  if (str_is_empty(method->source_file_name)) {
    res = sb_append(res, this_class_type->this_class_name, arena);
  } else {
    res = sb_append(res, method->source_file_name, arena);
  }

  if (method->source_line > 0) {
    res = sb_append_c(res, ":", arena);
    res = sb_append_u64(res, (u64)method->source_line, arena);
  }

  return sb_build(res);
}

static void resolver_remove_non_applicable_function_candidates(
    resolver_t *resolver, u32 *candidate_functions_i,
    const u32 *call_site_argument_types_i, arena_t scratch_arena,
    arena_t *arena) {

  u64 i = 0;
  while (i < pg_array_len(candidate_functions_i)) {
    const ty_type_t *const candidate_type =
        &resolver->types[candidate_functions_i[i]];
    if (!resolver_is_function_candidate_applicable(
            resolver, call_site_argument_types_i, candidate_type, scratch_arena,
            arena)) {
      pg_array_swap_remove_at(candidate_functions_i, i);
    } else {
      i++;
    }
  }
}

typedef enum __attribute__((packed)) {
  APPLICABILITY_LESS = 1,
  APPLICABILITY_SAME = 2,
  APPLICABILITY_MORE = 4,
} type_applicability_t;

static type_applicability_t resolver_check_applicability_of_candidate_pair(
    resolver_t *resolver, const ty_type_t *a, const ty_type_t *b,
    arena_t scratch_arena, arena_t *arena) {
  pg_assert(a->kind == TYPE_METHOD || a->kind == TYPE_CONSTRUCTOR);
  pg_assert(a->v.method.argument_types_i != NULL);
  pg_assert(a->this_class_name.data != NULL);
  pg_assert(a->this_class_name.len > 0);
  const u8 call_argument_count = pg_array_len(a->v.method.argument_types_i);

  pg_assert(b->kind == TYPE_METHOD || b->kind == TYPE_CONSTRUCTOR);
  pg_assert(b->v.method.argument_types_i != NULL);
  pg_assert(b->this_class_name.data != NULL);
  pg_assert(b->this_class_name.len > 0);
  pg_assert(pg_array_len(b->v.method.argument_types_i) == call_argument_count);

  for (u64 k = 0; k < call_argument_count; k++) {
    ty_type_t *argument_a = &resolver->types[a->v.method.argument_types_i[k]];
    const ty_type_t *argument_b =
        &resolver->types[b->v.method.argument_types_i[k]];

    if (!resolver_is_type_subtype_of(resolver, argument_a, argument_b,
                                     scratch_arena, arena)) {
      return APPLICABILITY_LESS;
    }
  }

  return APPLICABILITY_SAME | APPLICABILITY_MORE;
}

static u32 resolver_select_most_specific_candidate_function(
    resolver_t *resolver, u32 *candidates, arena_t scratch_arena,
    arena_t *arena) {

  const u64 candidates_count = pg_array_len(candidates);

  bool *tombstones = NULL;
  pg_array_init_reserve(tombstones, candidates_count, &scratch_arena);
  pg_array_header(tombstones)->len = candidates_count;
  memset(tombstones, false, pg_array_len(tombstones));
  u64 tombstones_count = 0;

  while (tombstones_count < candidates_count - 1) {
    for (u64 i = 0; i < candidates_count; i++) {
      for (u64 j = 0; j < i; j++) {
        const u32 a_index = i;
        const u32 b_index = j;
        const u32 a_type_i = candidates[a_index];
        const u32 b_type_i = candidates[b_index];

        if (tombstones[a_index] || tombstones[b_index])
          continue;

        const ty_type_t *const a = &resolver->types[a_type_i];
        const ty_type_t *const b = &resolver->types[b_type_i];

        const type_applicability_t a_b =
            resolver_check_applicability_of_candidate_pair(
                resolver, a, b, scratch_arena, arena);
        const type_applicability_t b_a =
            resolver_check_applicability_of_candidate_pair(
                resolver, b, a, scratch_arena, arena);

        const bool a_more_applicable_than_b = a_b & APPLICABILITY_MORE;
        const bool b_more_applicable_than_a = b_a & APPLICABILITY_MORE;

        if (cli_log_verbose) {
          str a_human_type = resolver_function_to_human_string(
              resolver, a_type_i, &scratch_arena);
          str b_human_type = resolver_function_to_human_string(
              resolver, b_type_i, &scratch_arena);

          LOG("[D001] %.*s vs %.*s: a_b=%u b_a=%u", (int)a_human_type.len,
              a_human_type.data, (int)b_human_type.len, b_human_type.data, a_b,
              b_a);

          if (a_more_applicable_than_b && !b_more_applicable_than_a) {
            LOG("[D002] removing %.*s", (int)b_human_type.len,
                b_human_type.data);
          }
          if (b_more_applicable_than_a && !a_more_applicable_than_b) {
            LOG("[D003] removing %.*s", (int)a_human_type.len,
                a_human_type.data);
          }
        }

        if (a_more_applicable_than_b && !b_more_applicable_than_a) {
          // A clearly more applicable than B, mark B as deleted so that it will
          // be skipped for subsequent checks.
          tombstones[b_index] = true;
          tombstones_count += 1;
        } else if (b_more_applicable_than_a && !a_more_applicable_than_b) {
          // B clearly more applicable than A, mark A as deleted so that it will
          // be skipped for subsequent checks.
          tombstones[a_index] = true;
          tombstones_count += 1;
        } else {
          // TODO: Need to do more checks.
        }
      }
    }
  }

  for (u64 i = 0; i < pg_array_len(tombstones); i++) {
    if (!tombstones[i])
      return candidates[i];
  }

  pg_assert(0 && "unreachable");
}

// TODO: Keep track of imported packages (including those imported by
// default).
static bool resolver_resolve_free_function(
    resolver_t *resolver, str method_name,
    const u32 *call_site_argument_types_i, u32 *picked_method_type_i,
    u32 **candidate_functions_i, arena_t scratch_arena, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(method_name.len > 0);
  pg_assert(picked_method_type_i != NULL);

  resolver_collect_callables_with_name(resolver, method_name,
                                       candidate_functions_i, arena);

  const u64 original_candidates_len = pg_array_len(*candidate_functions_i);

  resolver_remove_non_applicable_function_candidates(
      resolver, *candidate_functions_i, call_site_argument_types_i,
      scratch_arena, arena);

  if (pg_array_len(*candidate_functions_i) == 0) {
    // Restore the original length to display the possible candidates in the
    // error message.
    pg_array_header(*candidate_functions_i)->len = original_candidates_len;
    return false;
  }
  if (pg_array_len(*candidate_functions_i) == 1) {
    *picked_method_type_i = (*candidate_functions_i)[0];
    return true;
  }

  *picked_method_type_i = resolver_select_most_specific_candidate_function(
      resolver, *candidate_functions_i, scratch_arena, arena);

  return true;
}

// TODO: Remove.
static bool ty_merge_types(const resolver_t *resolver, u32 lhs_i, u32 rhs_i,
                           u32 *result_i) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->types != NULL);
  pg_assert(lhs_i < pg_array_len(resolver->types));
  pg_assert(rhs_i < pg_array_len(resolver->types));
  pg_assert(result_i != NULL);

  const ty_type_t *const lhs_type = &resolver->types[lhs_i];
  const ty_type_t *const rhs_type = &resolver->types[rhs_i];

  if (resolver_are_types_equal(resolver, lhs_type, rhs_type)) {
    *result_i = lhs_i;
    return true;
  }

  // Any is compatible with everything.
  if (lhs_type->kind == TYPE_ANY) {
    *result_i = rhs_i;
    return true;
  }

  // Any is compatible with everything.
  if (rhs_type->kind == TYPE_ANY) {
    *result_i = lhs_i;
    return true;
  }

  // FIXME: Widen.
  if ((lhs_type->kind == TYPE_INT) && (rhs_type->kind == TYPE_BYTE)) {
    *result_i = rhs_i;
    return true;
  }

  if ((lhs_type->kind == TYPE_BYTE) && (rhs_type->kind == TYPE_INT)) {
    *result_i = lhs_i;
    return true;
  }

  if ((lhs_type->kind == TYPE_INT) && (rhs_type->kind == TYPE_SHORT)) {
    *result_i = rhs_i;
    return true;
  }

  if ((lhs_type->kind == TYPE_SHORT) && (rhs_type->kind == TYPE_INT)) {
    *result_i = lhs_i;
    return true;
  }

  return false;
}

static void resolver_ast_fprint_node(const resolver_t *resolver, u32 node_i,
                                     FILE *file, u16 indent, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->parser != NULL);
  pg_assert(resolver->parser->lexer != NULL);
  pg_assert(resolver->parser->lexer->tokens != NULL);
  pg_assert(resolver->parser->nodes != NULL);
  pg_assert(resolver->parser->tokens_i <=
            pg_array_len(resolver->parser->lexer->tokens));
  pg_assert(node_i < pg_array_len(resolver->parser->nodes));

  if (!cli_log_verbose)
    return;

  const par_ast_node_t *const node = &resolver->parser->nodes[node_i];
  if (node->kind == AST_KIND_NONE)
    return;

  const str kind_string = par_ast_node_kind_to_string[node->kind];
  const lex_token_t token = resolver->parser->lexer->tokens[node->main_token_i];
  u32 line = 0;
  u32 column = 0;
  str token_string = {0};
  par_find_token_position(resolver->parser, token, &line, &column,
                          &token_string);

  ut_fwrite_indent(file, indent);

  const char *const type_kind =
      ty_type_kind_string(resolver->types, node->type_i);
  str human_type =
      ty_type_to_human_string(resolver->types, node->type_i, arena);

  pg_assert(indent < UINT16_MAX - 1); // Avoid overflow.
  switch (node->kind) {
  case AST_KIND_BOOL:
    LOG("[%ld] %.*s %.*s : %.*s (%s) (at %.*s:%u:%u:%u)",
        node - resolver->parser->nodes, (int)kind_string.len, kind_string.data,
        (int)token_string.len, token_string.data, (int)human_type.len,
        human_type.data, type_kind, (int)resolver->parser->lexer->file_path.len,
        resolver->parser->lexer->file_path.data, line, column,
        token.source_offset);
    break;

  case AST_KIND_LIST:
    LOG("[%ld] %.*s %.*s : %.*s %s (at %.*s:%u:%u:%u), %lu children",
        node - resolver->parser->nodes, (int)kind_string.len, kind_string.data,
        (int)token_string.len, token_string.data, (int)human_type.len,
        human_type.data, type_kind, (int)resolver->parser->lexer->file_path.len,
        resolver->parser->lexer->file_path.data, line, column,
        token.source_offset, pg_array_len(node->nodes));

    for (u64 i = 0; i < pg_array_len(node->nodes); i++)
      resolver_ast_fprint_node(resolver, node->nodes[i], file, indent + 2,
                               arena);
    break;
  case AST_KIND_CALL: {

    human_type =
        resolver_function_to_human_string(resolver, node->type_i, arena);
    LOG("[%ld] %.*s %.*s : %.*s %s (at %.*s:%u:%u:%u), %lu children",
        node - resolver->parser->nodes, (int)kind_string.len, kind_string.data,
        (int)token_string.len, token_string.data, (int)human_type.len,
        human_type.data, type_kind, (int)resolver->parser->lexer->file_path.len,
        resolver->parser->lexer->file_path.data, line, column,
        token.source_offset, pg_array_len(node->nodes));

    for (u64 i = 0; i < pg_array_len(node->nodes); i++)
      resolver_ast_fprint_node(resolver, node->nodes[i], file, indent + 2,
                               arena);
    break;
  }
  default:
    LOG("[%ld] %.*s %.*s : %.*s %s (at %.*s:%u:%u:%u)",
        node - resolver->parser->nodes, (int)kind_string.len, kind_string.data,
        (int)token_string.len, token_string.data, (int)human_type.len,
        human_type.data, type_kind, (int)resolver->parser->lexer->file_path.len,
        resolver->parser->lexer->file_path.data, line, column,
        token.source_offset);
    resolver_ast_fprint_node(resolver, node->lhs, file, indent + 2, arena);
    resolver_ast_fprint_node(resolver, node->rhs, file, indent + 2, arena);
    break;
  }
}

#define TYPE_ANY_I ((u32)0)
#define TYPE_UNIT_I ((u32)1)
#define TYPE_BOOLEAN_I ((u32)2)
#define TYPE_BYTE_I ((u32)3)
#define TYPE_CHAR_I ((u32)4)
#define TYPE_DOUBLE_I ((u32)5)
#define TYPE_FLOAT_I ((u32)6)
#define TYPE_INT_I ((u32)7)
#define TYPE_LONG_I ((u32)8)
#define TYPE_SHORT_I ((u32)9)
#define TYPE_STRING_I ((u32)10)

static bool type_fqn_equal_to_package_and_name(str a_fqn, str b_package_name,
                                               str b_class_name,
                                               arena_t scratch_arena) {
  pg_assert(!str_contains_element(b_class_name, (u8)'/'));
  pg_assert(!str_contains_element(b_class_name, (u8)'.'));

  str_builder b_fqn =
      sb_new(b_package_name.len + 1 + b_class_name.len, &scratch_arena);
  if (!str_is_empty(b_package_name)) {
    b_fqn = sb_append(b_fqn, b_package_name, &scratch_arena);
    b_fqn = sb_append_char(b_fqn, '.', &scratch_arena);
  }
  b_fqn = sb_append(b_fqn, b_class_name, &scratch_arena);

  return str_eq(a_fqn, sb_build(b_fqn));
}

static bool resolver_resolve_fully_qualified_name(resolver_t *resolver, str fqn,
                                                  u32 *type_i,
                                                  arena_t scratch_arena,
                                                  arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(!str_is_empty(fqn));
  pg_assert(type_i != NULL);
  pg_assert(arena != NULL);

  // The JVM uses `/` but Java and Kotlin use `.` as separator.
  pg_assert(!str_contains_element(fqn, '/'));

  // TODO: Flag types coming from java as nullable.

  if (str_eq_c(fqn, "kotlin.Any")) {
    *type_i = TYPE_ANY_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Unit")) {
    *type_i = TYPE_UNIT_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Boolean") ||
             str_eq_c(fqn, "java/lang/Boolean")) {
    *type_i = TYPE_BOOLEAN_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Byte") || str_eq_c(fqn, "java/lang/Byte")) {
    *type_i = TYPE_BYTE_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Char") || str_eq_c(fqn, "java/lang/Char")) {
    *type_i = TYPE_CHAR_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Short") ||
             str_eq_c(fqn, "java/lang/Short")) {
    *type_i = TYPE_SHORT_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Int") ||
             str_eq_c(fqn, "java/lang/Integer")) {
    *type_i = TYPE_INT_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Float") ||
             str_eq_c(fqn, "java/lang/Float")) {
    *type_i = TYPE_FLOAT_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Long") || str_eq_c(fqn, "java/lang/Long")) {
    *type_i = TYPE_LONG_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.Double") ||
             str_eq_c(fqn, "java/lang/Double")) {
    *type_i = TYPE_DOUBLE_I;
    return true;
  } else if (str_eq_c(fqn, "kotlin.String") ||
             str_eq_c(fqn, "java/lang/String")) {
    *type_i = TYPE_STRING_I;
    return true;
  }

  // Check if cached first.
  for (u64 i = 0; i < pg_array_len(resolver->types); i++) {
    const ty_type_t *const type = &resolver->types[i];

    if (type->kind == TYPE_METHOD || type->kind == TYPE_CONSTRUCTOR)
      continue;

    if (type->kind == TYPE_INSTANCE &&
        type_fqn_equal_to_package_and_name(
            fqn, type->package_name, type->this_class_name, scratch_arena)) {
      *type_i = i;
      return true;
    }
  }

  // Scan the class path entries for `$CLASS_PATH_ENTRY/$CLASS_NAME.class`.
  // E.g.: `/usr/share/java/kotlin-stdlib.jar` -> `/usr/share/java/Fqn.class`.
  for (u64 i = 0; i < pg_array_len(resolver->class_path_entries); i++) {
    str entry = resolver->class_path_entries[i];
    str_split_result_t entry_last_slash_split = str_rsplit(entry, '/');
    str parent = entry_last_slash_split.left;

    str final_extension = str_from_c(".class");

    str_builder tentative_class_file_path_builder =
        sb_new(parent.len + 1 + fqn.len + final_extension.len, arena);

    tentative_class_file_path_builder =
        sb_append(tentative_class_file_path_builder, parent, arena);
    tentative_class_file_path_builder =
        sb_append_char(tentative_class_file_path_builder, '/', arena);

    {
      u64 replace_start = tentative_class_file_path_builder.len;

      // Transform e.g. `/a/b/kotlin.io.ConsoleKt` into
      // `/a/b/kotlin/io/ConsoleKt`.
      tentative_class_file_path_builder =
          sb_append(tentative_class_file_path_builder, fqn, arena);

      tentative_class_file_path_builder = sb_replace_element_starting_at(
          tentative_class_file_path_builder, replace_start, '.', '/');
    }

    tentative_class_file_path_builder =
        sb_append(tentative_class_file_path_builder, final_extension, arena);
    str tentative_class_file_path = sb_build(tentative_class_file_path_builder);

    {
      // TODO: check if we can read the file content into `scratch_arena`
      ut_read_result_t read_res = ut_read_all_from_file_path(
          tentative_class_file_path, scratch_arena, &scratch_arena);
      if (read_res.error) // Silently swallow the error and skip this entry.
        continue;

      cf_class_file_t class_file = {
          .class_file_path = tentative_class_file_path,
      };
      u8 *current = read_res.content.data;
      cf_buf_read_class_file(read_res.content, &current, &class_file, arena);

      pg_assert(str_eq(fqn, class_file.class_name));

      ty_type_t this_type = {.kind = TYPE_INSTANCE};
      type_init_package_and_name(class_file.class_name, &this_type.package_name,
                                 &this_type.this_class_name, arena);

      *type_i = resolver_add_type(resolver, &this_type, arena);

      return true;
    }
  }

  // Scan the class path entries for `$CLASS_PATH_ENTRY` which is a jar file
  // that contains
  // `$CLASS_NAME.class`.
  for (u64 i = 0; i < pg_array_len(resolver->class_path_entries); i++) {
    str class_path_entry = resolver->class_path_entries[i];
    if (!str_ends_with(class_path_entry, str_from_c(".jar")))
      continue;

    const u64 previous_len = pg_array_len(resolver->types);
    cf_read_jar_file(resolver, class_path_entry, scratch_arena, arena);

    for (u64 i = previous_len; i < pg_array_len(resolver->types); i++) {
      const ty_type_t *const type = &resolver->types[i];
      if (type->kind == TYPE_INSTANCE &&
          type_fqn_equal_to_package_and_name(
              fqn, type->package_name, type->this_class_name, scratch_arena)) {
        *type_i = i;
        return true;
      }
    }
  }

  return false;
}

// TODO: Check if there is a way not to do it lazily. Not goog to have I/O
// randomly pop up in the middle of type checking.
static bool resolver_resolve_super_lazily(resolver_t *resolver, ty_type_t *type,
                                          arena_t scratch_arena,
                                          arena_t *arena) {

  // Already done?
  if (type->super_type_i > 0)
    return true;

  if (str_eq_c(type->this_class_name, "java/lang/Object"))
    return true; // Reached the top.

  // Since most types inherit from Object, we do not allocate a string for it
  // for optimization purposes.
  if (str_is_empty(type->super_class_name)) {
    return resolver_resolve_fully_qualified_name(
        resolver, str_from_c("java.lang.Object"), &type->super_type_i,
        scratch_arena, arena);
  }

  return resolver_resolve_fully_qualified_name(resolver, type->super_class_name,
                                               &type->super_type_i,
                                               scratch_arena, arena);
}

static void resolver_load_standard_types(resolver_t *resolver, str java_home,
                                         arena_t scratch_arena,
                                         arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(!str_is_empty(java_home));
  pg_assert(arena != NULL);

  const ty_type_t known_types[] = {
      [TYPE_ANY_I] = {.kind = TYPE_ANY},
      [TYPE_BOOLEAN_I] = {.kind = TYPE_BOOLEAN},
      [TYPE_BYTE_I] = {.kind = TYPE_BYTE},
      [TYPE_CHAR_I] = {.kind = TYPE_CHAR},
      [TYPE_DOUBLE_I] = {.kind = TYPE_DOUBLE},
      [TYPE_FLOAT_I] = {.kind = TYPE_FLOAT},
      [TYPE_INT_I] = {.kind = TYPE_INT},
      [TYPE_LONG_I] = {.kind = TYPE_LONG},
      [TYPE_SHORT_I] = {.kind = TYPE_SHORT},
      [TYPE_STRING_I] = {.kind = TYPE_STRING},
      [TYPE_UNIT_I] = {.kind = TYPE_UNIT},
  };

  for (u64 i = 0; i < sizeof(known_types) / sizeof(known_types[0]); i++)
    pg_array_append(resolver->types, known_types[i], arena);

  str relative_jmod_path = str_from_c("/jmods/java.base.jmod");
  str_builder path = sb_new(java_home.len + relative_jmod_path.len, arena);
  path = sb_append(path, java_home, arena);
  path = sb_append(path, relative_jmod_path, arena);

  cf_read_jmod_file(resolver, sb_build(path), scratch_arena, arena);

  u32 dummy = 0;
  str sanity_check = str_from_c("kotlin.io.ConsoleKt");
  if (!resolver_resolve_fully_qualified_name(resolver, sanity_check, &dummy,
                                             scratch_arena, arena)) {
    fprintf(
        stderr,
        "Could not load the kotlin stdlib classes (failed to load the class "
        "`%.*s` as sanity check for `println` functions). "
        "Please provide the CLI "
        "option manually e.g.: \"-c /usr/share/java/kotlin-stdlib.jar\".\n",
        (int)sanity_check.len, sanity_check.data);
    exit(ENOENT);
  }
}

static void ty_begin_scope(resolver_t *resolver) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->scope_depth < UINT32_MAX);

  resolver->scope_depth += 1;
}

static void ty_end_scope(resolver_t *resolver) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->scope_depth > 0);

  for (i64 i = pg_array_len(resolver->variables) - 1; i >= 0; i--) {
    const ty_variable_t *const variable = &resolver->variables[i];
    if (variable->scope_depth == resolver->scope_depth)
      pg_array_drop_last(resolver->variables);
    else if (variable->scope_depth < resolver->scope_depth)
      break;
    else
      pg_assert(0 && "unreachable");
  }
  resolver->scope_depth -= 1;
}

static u32 ty_declare_variable(resolver_t *resolver, str name, u32 node_i,
                               arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->scope_depth > 0);
  pg_assert(resolver->variables != NULL);
  pg_assert(arena != NULL);
  pg_assert(node_i > 0);

  pg_assert(pg_array_len(resolver->variables) < UINT32_MAX);

  const ty_variable_t variable = {
      .name = name,
      .scope_depth = -1, // Uninitialized.
      .var_definition_node_i = node_i,
  };
  pg_array_append(resolver->variables, variable, arena);
  return pg_array_last_index(resolver->variables);
}

static void ty_mark_variable_as_initialized(resolver_t *resolver,
                                            u32 variable_i) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->scope_depth > 0);
  pg_assert(variable_i < pg_array_len(resolver->variables));

  // Should this function be idempotent? In that case, this assert should be
  // removed.
  pg_assert(resolver->variables[variable_i].scope_depth == (u32)-1);

  resolver->variables[variable_i].scope_depth = resolver->scope_depth;
}

static u32 ty_find_variable(resolver_t *resolver, u32 token_i) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->scope_depth > 0);
  pg_assert(resolver->variables != NULL);

  str name = par_token_to_str_view(resolver->parser, token_i);

  for (i64 i = pg_array_len(resolver->variables) - 1; i >= 0; i--) {
    const ty_variable_t *const variable = &resolver->variables[i];
    if (!str_eq(name, variable->name))
      continue;

    if (variable->scope_depth == (u32)-1) {
      par_error(resolver->parser, resolver->parser->lexer->tokens[token_i],
                "Cannot read local variable in its own initializer");
      return -1;
    }
    return (u32)i;
  }

  return -1;
}

static str resolver_get_fqn_from_navigation_chain(const resolver_t *resolver,
                                                  u32 node_i) {
  const par_ast_node_t *const node = &resolver->parser->nodes[node_i];
  pg_assert(node->kind == AST_KIND_NAVIGATION);

  const lex_token_t start = resolver->parser->lexer->tokens[node->main_token_i];
  pg_assert(start.kind == TOKEN_KIND_IDENTIFIER);

  u32 end_token_excl_i = node->main_token_i + 1;
  while (end_token_excl_i < pg_array_len(resolver->parser->lexer->tokens)) {
    const lex_token_t current =
        resolver->parser->lexer->tokens[end_token_excl_i];
    if (!(current.kind == TOKEN_KIND_IDENTIFIER ||
          current.kind == TOKEN_KIND_DOT))
      break;

    end_token_excl_i += 1;
  }

  return par_token_range_to_string(resolver->parser, node->main_token_i,
                                   end_token_excl_i);
}

static bool ty_variable_shadows(resolver_t *resolver, u32 name_token_i) {

  const u32 previous_var_i = ty_find_variable(resolver, name_token_i);
  if (previous_var_i == (u32)-1)
    return false;

  pg_assert(previous_var_i < pg_array_len(resolver->variables));
  const ty_variable_t *const previous_var =
      &resolver->variables[previous_var_i];

  pg_assert(previous_var->var_definition_node_i > 0);
  pg_assert(previous_var->var_definition_node_i <
            pg_array_len(resolver->parser->nodes));
  const par_ast_node_t *const previous_var_node =
      &resolver->parser->nodes[previous_var->var_definition_node_i];

  const lex_token_t previous_var_name_token =
      resolver->parser->lexer->tokens[previous_var_node->main_token_i];

  u32 line = 0;
  u32 column = 0;
  str token_string = {0};
  par_find_token_position(resolver->parser, previous_var_name_token, &line,
                          &column, &token_string);
  char error[256] = {0};
  snprintf(error, 255, "variable shadowing, already declared at %u:%u", line,
           column);
  par_error(resolver->parser, resolver->parser->lexer->tokens[name_token_i],
            error);
  return false;
}

static u32 resolver_resolve_node(resolver_t *resolver, u32 node_i,
                                 arena_t scratch_arena, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(resolver->parser != NULL);
  pg_assert(node_i < pg_array_len(resolver->parser->nodes));

  par_ast_node_t *const node = &resolver->parser->nodes[node_i];
  const lex_token_t token = resolver->parser->lexer->tokens[node->main_token_i];

  switch (node->kind) {
  case AST_KIND_NONE:
    pg_array_append(resolver->types, (ty_type_t){.kind = TYPE_ANY}, arena);

    return node->type_i = pg_array_last_index(resolver->types);
  case AST_KIND_BOOL: {
    return node->type_i = TYPE_BOOLEAN_I;
  }
  case AST_KIND_CALL: {
    arena_t tmp_arena = scratch_arena;

    const par_ast_node_t *const lhs = &resolver->parser->nodes[node->lhs];
    pg_assert(lhs->kind == AST_KIND_UNRESOLVED_NAME);
    str name = par_token_to_str_view(resolver->parser, lhs->main_token_i);

    // Resolve arguments.
    u32 *call_site_argument_types_i = NULL;
    pg_array_init_reserve(call_site_argument_types_i, pg_array_len(node->nodes),
                          &tmp_arena);

    for (u64 i = 0; i < pg_array_len(node->nodes); i++) {
      const u32 node_i = node->nodes[i];
      pg_assert(node_i > 0);

      const u32 type_i =
          resolver_resolve_node(resolver, node_i, tmp_arena, arena);
      pg_array_append(call_site_argument_types_i, type_i, &tmp_arena);
    }

    u32 picked_method_type_i = 0;
    {
      u32 *candidate_functions_i = NULL;
      pg_array_init_reserve(candidate_functions_i, 128, &tmp_arena);

      if (!resolver_resolve_free_function(
              resolver, name, call_site_argument_types_i, &picked_method_type_i,
              &candidate_functions_i, tmp_arena, arena)) {

        str_builder error = sb_new(256, &scratch_arena);
        error = sb_append_c(error, "failed to find matching function", arena);

        if (pg_array_len(candidate_functions_i) == 0) {
          error = sb_append_c(
              error, ", could not find any function with this name: ", arena);
          error = sb_append(error, name, arena);
        } else {
          error = sb_append_c(error, ", possible candidates: ", arena);

          for (u64 i = 0; i < pg_array_len(candidate_functions_i); i++) {
            const u32 candidate_type_i = candidate_functions_i[i];

            error = sb_append_c(error, "\n  ", arena);
            error = sb_append(error,
                              resolver_function_to_human_string(
                                  resolver, candidate_type_i, arena),
                              arena);
          }
        }
        par_error(resolver->parser, token, (char *)error.data);
        return 0;
      }
    }

    pg_assert(picked_method_type_i > 0);
    const ty_type_t *picked_method_type =
        &resolver->types[picked_method_type_i];
    pg_assert(picked_method_type->kind == TYPE_METHOD ||
              picked_method_type->kind == TYPE_CONSTRUCTOR);

    node->type_i = picked_method_type_i;

    return picked_method_type->v.method.return_type_i;
  }
  case AST_KIND_NUMBER: {
    u8 flag = 0;
    const u64 number = par_number(resolver->parser, token, &flag);
    if (flag & NODE_NUMBER_FLAGS_OVERFLOW) {
      par_error(resolver->parser, token,
                "Integer literal is too big (> 9223372036854775807)");
      return 0;

    } else if (flag & NODE_NUMBER_FLAGS_LONG) {
      node->type_i = TYPE_LONG_I;
    } else {
      // >  it has an integer literal type containing all the
      // built-in integer types guaranteed to be able to represent this value.

      if (number <= INT32_MAX) {
        node->type_i = TYPE_INT_I;
      } else {
        node->type_i = TYPE_LONG_I;
      }
    }
    node->extra_data_i = number;

    return node->type_i;
  }
  case AST_KIND_UNARY:
    switch (token.kind) {
    case TOKEN_KIND_NOT:
      node->type_i =
          resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
      const ty_type_t *const type = &resolver->types[node->type_i];
      if (type->kind != TYPE_BOOLEAN) {
        str_builder error = sb_new(256, &scratch_arena);
        error = sb_append_c(error, "incompatible types: got ", arena);
        error = sb_append(
            error,
            ty_type_to_human_string(resolver->types, node->type_i, arena),
            arena);
        error = sb_append_c(error, ", expected Boolean ", arena);
        par_error(resolver->parser, token, (char *)error.data);
        return 0;
      }

      return node->type_i;

    case TOKEN_KIND_MINUS:
      return node->type_i = resolver_resolve_node(resolver, node->lhs,
                                                  scratch_arena, arena);

    default:
      pg_assert(0 && "todo");
    }
    break;
  case AST_KIND_BINARY: {
    pg_assert(node->main_token_i > 0);

    const u32 lhs_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    const u32 rhs_i =
        resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);

    if (!ty_merge_types(resolver, lhs_i, rhs_i, &node->type_i)) {
      str_builder error = sb_new(256, &scratch_arena);
      error = sb_append_c(error, "incompatible types: ", arena);
      error = sb_append(
          error, ty_type_to_human_string(resolver->types, lhs_i, arena), arena);
      error = sb_append_c(error, " vs ", arena);
      error = sb_append(
          error, ty_type_to_human_string(resolver->types, rhs_i, arena), arena);
      par_error(resolver->parser, token, (char *)error.data);
    }

    switch (token.kind) {
    case TOKEN_KIND_LT:
    case TOKEN_KIND_LE:
    case TOKEN_KIND_GT:
    case TOKEN_KIND_GE:
    case TOKEN_KIND_NOT_EQUAL:
    case TOKEN_KIND_EQUAL_EQUAL: {
      return node->type_i = TYPE_BOOLEAN_I;
    }
    case TOKEN_KIND_AMPERSAND_AMPERSAND:
    case TOKEN_KIND_PIPE_PIPE: {
      const ty_type_t *const lhs_type = &resolver->types[lhs_i];
      if (lhs_type->kind != TYPE_BOOLEAN) {
        str_builder error = sb_new(256, &scratch_arena);
        error = sb_append_c(
            error, "incompatible types: expected Boolean, got: ", arena);
        error = sb_append(
            error, ty_type_to_human_string(resolver->types, lhs_i, arena),
            arena);
        par_error(resolver->parser, token, (char *)error.data);
      }
      return 0;
    }
      return node->type_i;
    default:
      return node->type_i;
    }
  }
  case AST_KIND_LIST: {
    for (u64 i = 0; i < pg_array_len(node->nodes); i++) {
      resolver_resolve_node(resolver, node->nodes[i], scratch_arena, arena);
      // Clean up after each statement.
      resolver->current_type_i = 0;
    }

    return node->type_i = TYPE_UNIT_I;
  }
  case AST_KIND_FUNCTION_DEFINITION: {
    // Already resolved by resolver_collect_user_defined_function_signatures().
    pg_assert(node->type_i > 0);

    ty_begin_scope(resolver);
    // Arguments (lhs).
    // Need to re-process them to have the right variables (the function
    // arguments) in the current scope.
    // TODO: We could optimize it by not creating new types at this point.
    resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);

    resolver->current_function_i = node_i;

    // Inspect body (rhs).
    resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);

    ty_end_scope(resolver);

    resolver->current_function_i = 0;

    return node->type_i;
  }

  case AST_KIND_VAR_DEFINITION: {
    pg_assert(node->lhs > 0); // TODO: The type is actually optional.

    if (ty_variable_shadows(resolver, node->main_token_i))
      return 0;

    const u32 variable_i = ty_declare_variable(
        resolver, par_token_to_str_view(resolver->parser, node->main_token_i),
        node_i, arena);

    const u32 lhs_type_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    const u32 rhs_type_i =
        resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);

    if (!ty_merge_types(resolver, lhs_type_i, rhs_type_i, &node->type_i)) {
      str lhs_type_human =
          ty_type_to_human_string(resolver->types, lhs_type_i, arena);
      str rhs_type_human =
          ty_type_to_human_string(resolver->types, rhs_type_i, arena);

      str_builder error = sb_new(256, &scratch_arena);
      error =
          sb_append_c(error, "incompatible types: declared type is ", arena);
      error = sb_append(error, lhs_type_human, arena);
      error = sb_append_c(error, ", received type is ", arena);
      error = sb_append(error, rhs_type_human, arena);
      par_error(resolver->parser, token, (char *)error.data);

      // Still assign a type to be able to proceed to catch as many errors
      // as possible.
      node->type_i = lhs_type_i;
    }

    ty_mark_variable_as_initialized(resolver, variable_i);

    return node->type_i;
  }
  case AST_KIND_VAR_REFERENCE: {
    pg_assert(node->lhs > 0);
    return node->type_i = resolver->parser->nodes[node->lhs].type_i;
  }
  case AST_KIND_IF: {
    pg_assert(node->lhs > 0);
    pg_assert(node->lhs < pg_array_len(resolver->parser->nodes));
    pg_assert(node->rhs > 0);
    pg_assert(node->rhs < pg_array_len(resolver->parser->nodes));

    const u32 type_condition_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    const ty_type_t *const type_condition = &resolver->types[type_condition_i];

    if (type_condition->kind != TYPE_BOOLEAN) {
      str_builder error = sb_new(256, &scratch_arena);
      error = sb_append_c(error,
                          "incompatible types, expected Boolean, got: ", arena);

      str type_inferred_string =
          ty_type_to_human_string(resolver->types, type_condition_i, arena);
      error = sb_append(error, type_inferred_string, arena);
      par_error(resolver->parser, token, (char *)error.data);
    }

    return node->type_i =
               resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);
  }
  case AST_KIND_WHILE_LOOP: {
    pg_assert(node->lhs > 0);
    pg_assert(node->lhs < pg_array_len(resolver->parser->nodes));
    pg_assert(node->rhs < pg_array_len(resolver->parser->nodes));

    const u32 type_condition_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    const ty_type_t *const type_condition = &resolver->types[type_condition_i];

    if (type_condition->kind != TYPE_BOOLEAN) {
      str_builder error = sb_new(256, &scratch_arena);
      error = sb_append_c(error,
                          "incompatible types, expect Boolean, got: ", arena);

      str type_inferred_string =
          ty_type_to_human_string(resolver->types, type_condition_i, arena);
      error = sb_append(error, type_inferred_string, arena);
      par_error(resolver->parser, token, (char *)error.data);
    }

    ty_begin_scope(resolver);
    resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);
    ty_end_scope(resolver);

    return node->type_i = TYPE_UNIT_I;
  }
  case AST_KIND_STRING: {
    return node->type_i = TYPE_STRING_I;
  }

  case AST_KIND_CLASS_REFERENCE: {
    pg_assert(0 && "todo");
  }

  case AST_KIND_NAVIGATION: { // e.g.: `foo.bar.baz`
    // IDEA:
    // If the first element `foo` is a known variable in scope: resolve that
    // recursively.
    // Else: try to load the package `foo.bar` and find the class
    // `baz`, or the function `public static (WhateverKt).baz`. Update the node
    // kind to `CLASS_REFERENCE` or `CALL` (or `FUNCTION_REFERENCE` ?).
    // Else: error.
    // TODO: static fields/companion objects.

    const u32 variable_i = ty_find_variable(resolver, node->main_token_i);

    if (variable_i == (u32)-1) {
      str fqn = resolver_get_fqn_from_navigation_chain(resolver, node_i);

      if (resolver_resolve_fully_qualified_name(resolver, fqn, &node->type_i,
                                                scratch_arena, arena)) {
        pg_assert(0 && "todo");
      } else {
        const lex_token_t main_token =
            resolver->parser->lexer->tokens[node->main_token_i];
        str_builder error = sb_new(256, &scratch_arena);
        error = sb_append_c(error,
                            "Unknown reference to a name, neither a variable "
                            "in scope or an external identifier: ",
                            arena);
        error = sb_append(error, fqn, arena);
        par_error(resolver->parser, main_token, (char *)error.data);
        return 0;
      }
    } else {
      const par_ast_node_t *const variable =
          &resolver->parser->nodes[variable_i];
      node->type_i = variable->type_i;

      return resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    }
    break;
  }

  case AST_KIND_FUNCTION_PARAMETER: {
    const u32 variable_i = ty_declare_variable(
        resolver, par_token_to_str_view(resolver->parser, node->main_token_i),
        node_i, arena);
    node->type_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    ty_mark_variable_as_initialized(resolver, variable_i);

    return node->type_i;
  }

  case AST_KIND_TYPE: {
    str type_literal_string =
        par_token_to_str_view(resolver->parser, node->main_token_i);

    if (str_eq_c(type_literal_string, "Any") ||
        str_eq_c(type_literal_string, "kotlin.Any")) {
      node->type_i = TYPE_ANY_I;
    } else if (str_eq_c(type_literal_string, "Unit") ||
               str_eq_c(type_literal_string, "kotlin.Unit")) {
      node->type_i = TYPE_UNIT_I;
    } else if (str_eq_c(type_literal_string, "Int") ||
               str_eq_c(type_literal_string, "kotlin.Int")) {
      node->type_i = TYPE_INT_I;
    } else if (str_eq_c(type_literal_string, "Boolean") ||
               str_eq_c(type_literal_string, "kotlin.Boolean")) {
      node->type_i = TYPE_BOOLEAN_I;
    } else if (str_eq_c(type_literal_string, "Byte") ||
               str_eq_c(type_literal_string, "kotlin.Byte")) {
      node->type_i = TYPE_BYTE_I;
    } else if (str_eq_c(type_literal_string, "Char") ||
               str_eq_c(type_literal_string, "kotlin.Char")) {
      node->type_i = TYPE_CHAR_I;
    } else if (str_eq_c(type_literal_string, "Short") ||
               str_eq_c(type_literal_string, "kotlin.Short")) {
      node->type_i = TYPE_SHORT_I;
    } else if (str_eq_c(type_literal_string, "Float") ||
               str_eq_c(type_literal_string, "kotlin.Float")) {
      node->type_i = TYPE_FLOAT_I;
    } else if (str_eq_c(type_literal_string, "Double") ||
               str_eq_c(type_literal_string, "kotlin.Double")) {
      node->type_i = TYPE_DOUBLE_I;
    } else if (str_eq_c(type_literal_string, "Long") ||
               str_eq_c(type_literal_string, "kotlin.Long")) {
      node->type_i = TYPE_LONG_I;
    } else {
      const bool found = resolver_resolve_fully_qualified_name(
          resolver, type_literal_string, &node->type_i, scratch_arena, arena);
      if (!found) {
        str_builder error = sb_new(256, &scratch_arena);
        error = sb_append_c(error, "unknown type: ", arena);
        error = sb_append(error, type_literal_string, arena);

        par_error(resolver->parser, token, (char *)error.data);
        return 0;
      }
    }

    return node->type_i;
  }

  case AST_KIND_UNRESOLVED_NAME: {
    const u32 variable_i = ty_find_variable(resolver, node->main_token_i);

    if (variable_i == (u32)-1) {
      par_error(resolver->parser,
                resolver->parser->lexer->tokens[node->main_token_i],
                "unknown reference to variable");
      return 0;
    }

    node->kind = AST_KIND_VAR_REFERENCE;
    node->lhs = resolver->variables[variable_i].var_definition_node_i;

    return resolver_resolve_node(resolver, node_i, scratch_arena, arena);

    break;
  }

  case AST_KIND_THEN_ELSE:
    ty_begin_scope(resolver);
    const u32 lhs_type_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    ty_end_scope(resolver);

    ty_begin_scope(resolver);
    const u32 rhs_type_i =
        resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);
    ty_end_scope(resolver);

    if (!ty_merge_types(resolver, lhs_type_i, rhs_type_i, &node->type_i)) {
      str_builder error = sb_new(256, &scratch_arena);
      error = sb_append_c(error, "incompatible types: ", arena);
      error = sb_append(
          error, ty_type_to_human_string(resolver->types, lhs_type_i, arena),
          arena);
      error = sb_append_c(error, " vs ", arena);
      error = sb_append(
          error, ty_type_to_human_string(resolver->types, rhs_type_i, arena),
          arena);
      par_error(resolver->parser, token, (char *)error.data);
    }
    return node->type_i;

    break;

  case AST_KIND_ASSIGNMENT:
    resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);

    if (!par_is_lvalue(resolver->parser, node->lhs)) {
      par_error(resolver->parser,
                resolver->parser->lexer->tokens[node->main_token_i],
                "The assignment target is not a lvalue (such as a local "
                "variable)");
    }

    return node->type_i =
               resolver_resolve_node(resolver, node->rhs, scratch_arena, arena);

  case AST_KIND_RETURN: {
    pg_assert(resolver->current_function_i);
    node->type_i =
        resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    const par_ast_node_t *const current_function =
        &resolver->parser->nodes[resolver->current_function_i];
    const ty_type_t *const function_type =
        &resolver->types[current_function->type_i];

    pg_assert(function_type->kind == TYPE_METHOD ||
              function_type->kind == TYPE_CONSTRUCTOR);
    const u32 return_type_i = function_type->v.method.return_type_i;

    if (!resolver_are_types_equal(resolver, &resolver->types[node->type_i],
                                  &resolver->types[return_type_i])) {
      str_builder error = sb_new(256, &scratch_arena);
      error =
          sb_append_c(error, "incompatible return type in function `", arena);
      error = sb_append(error,
                        par_token_to_str_view(resolver->parser,
                                              current_function->main_token_i),
                        arena);
      error = sb_append_c(error, "` of type ", arena);
      error = sb_append(error,
                        ty_type_to_human_string(
                            resolver->types, current_function->type_i, arena),
                        arena);
      error = sb_append_c(error, ": got ", arena);

      error = sb_append(
          error, ty_type_to_human_string(resolver->types, node->type_i, arena),
          arena);
      error = sb_append_c(error, ", expected ", arena);
      error = sb_append(
          error, ty_type_to_human_string(resolver->types, return_type_i, arena),
          arena);
      par_error(resolver->parser, token, (char *)error.data);
    }

    return node->type_i;
  }

  case AST_KIND_MAX:
    pg_assert(0 && "unreachable");
  }
  pg_assert(0 && "unreachable");
}

static void resolver_collect_user_defined_function_signatures(
    resolver_t *resolver, arena_t scratch_arena, arena_t *arena) {
  for (u64 i = 0; i < pg_array_len(resolver->parser->nodes); i++) {
    par_ast_node_t *const node = &resolver->parser->nodes[i];
    if (node->kind != AST_KIND_FUNCTION_DEFINITION)
      continue;

    ty_begin_scope(resolver);
    resolver->current_function_i = i;

    // Arguments (lhs).
    resolver_resolve_node(resolver, node->lhs, scratch_arena, arena);
    // Return type, if present.
    u32 return_type_i = TYPE_UNIT_I;
    if (node->extra_data_i > 0) {
      return_type_i = resolver_resolve_node(resolver, node->extra_data_i,
                                            scratch_arena, arena);
    }

    const lex_token_t name_token =
        resolver->parser->lexer->tokens[node->main_token_i];
    ty_type_t type = {
        .kind = TYPE_METHOD,
        .package_name = resolver->parser->current_package,
        .this_class_name = resolver->this_class_name,
        .v = {.method =
                  {
                      .return_type_i = return_type_i,
                      .source_file_name = resolver->parser->lexer->file_path,
                      .access_flags = ACCESS_FLAGS_PUBLIC | ACCESS_FLAGS_STATIC,
                  }},
    };
    u32 column = 0;
    u32 line = 0;
    par_find_token_position(resolver->parser, name_token, &line, &column,
                            &type.v.method.name);
    pg_assert(line <= UINT16_MAX);
    type.v.method.source_line = (u16)line;

    if (node->lhs > 0) {
      const par_ast_node_t *const lhs = &resolver->parser->nodes[node->lhs];
      pg_assert(lhs->kind == AST_KIND_LIST);

      pg_array_init_reserve(type.v.method.argument_types_i,
                            pg_array_len(lhs->nodes), arena);
      for (u64 i = 0; i < pg_array_len(lhs->nodes); i++) {
        const u32 node_i = lhs->nodes[i];
        const u32 type_i = resolver->parser->nodes[node_i].type_i;
        pg_array_append(type.v.method.argument_types_i, type_i, arena);
      }
    }

    node->type_i = resolver_add_type(resolver, &type, arena);

    // NOTE: Skip function body by nature.
    // But: Once we allow return type inference based on body, we need to also
    // inspect the body.

    resolver->current_function_i = 0;
    ty_end_scope(resolver);
  }
}

// --------------------------------- Code generation

typedef struct {
  cf_variable_t variable;
  u32 scope_id;
} cg_scope_variable_t;

typedef struct {
  resolver_t *resolver;
  cf_attribute_code_t *code;
  cg_frame_t *frame;
  cg_scope_variable_t *locals;
  cf_stack_map_frame_t *stack_map_frames;
  u32 scope_id;
  pg_pad(4);
} cg_generator_t;

// FIXME: Probably should not behave like a FIFO and rather like an array.
static void cg_frame_locals_push(cg_generator_t *gen,
                                 const cf_variable_t *variable,
                                 u16 *logical_local_index,
                                 u16 *physical_local_index, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(variable != NULL);
  pg_assert(arena != NULL);
  pg_assert(logical_local_index != NULL);
  pg_assert(physical_local_index != NULL);

  pg_assert(gen->frame->locals_physical_count + 1 < UINT16_MAX);

  pg_array_append(gen->frame->locals, *variable, arena);

  const u64 word_count =
      cf_verification_info_kind_word_count(variable->verification_info.kind);

  *logical_local_index = pg_array_last_index(gen->frame->locals);
  *physical_local_index = gen->frame->locals_physical_count;
  gen->frame->locals_physical_count += word_count;
  gen->frame->max_physical_locals = pg_max(gen->frame->max_physical_locals,
                                           gen->frame->locals_physical_count);

  cg_scope_variable_t scope_variable = {.variable = *variable,
                                        .scope_id = gen->scope_id};
  pg_array_append(gen->locals, scope_variable, arena);
}

static u16 cg_import_constant(cf_constant_array_t *dst,
                              const cf_constant_array_t *src, u16 constant_i,
                              arena_t *arena) {
  const cf_constant_t *const constant = cf_constant_array_get(src, constant_i);

  switch (constant->kind) {
  case CONSTANT_POOL_KIND_FIELD_REF: {
    const cf_constant_field_ref_t field_ref = constant->v.field_ref;

    cf_constant_t constant_gen = {
        .kind = constant->kind,
        .v =
            {
                .field_ref =
                    {
                        .descriptor = cg_import_constant(
                            dst, src, field_ref.descriptor, arena),
                        .name =
                            cg_import_constant(dst, src, field_ref.name, arena),
                    },
            },
    };
    return cf_constant_array_push(dst, &constant_gen, arena);
  }
  case CONSTANT_POOL_KIND_METHOD_REF: {
    const cf_constant_method_ref_t method_ref = constant->v.method_ref;

    cf_constant_t constant_gen = {
        .kind = constant->kind,
        .v =
            {
                .method_ref =
                    {
                        .class = cg_import_constant(dst, src, method_ref.class,
                                                    arena),
                        .name_and_type = cg_import_constant(
                            dst, src, method_ref.name_and_type, arena),
                    },
            },
    };
    return cf_constant_array_push(dst, &constant_gen, arena);
  }

  case CONSTANT_POOL_KIND_NAME_AND_TYPE: {
    const cf_constant_name_and_type_t name_and_type = constant->v.name_and_type;
    cf_constant_t constant_gen = {
        .kind = constant->kind,
        .v =
            {
                .name_and_type =
                    {
                        .name = cg_import_constant(dst, src, name_and_type.name,
                                                   arena),
                        .descriptor = cg_import_constant(
                            dst, src, name_and_type.descriptor, arena),
                    },
            },
    };
    return cf_constant_array_push(dst, &constant_gen, arena);
  }

  case CONSTANT_POOL_KIND_INT:
  case CONSTANT_POOL_KIND_UTF8: {
    return cf_constant_array_push(dst, constant, arena);
  }

  case CONSTANT_POOL_KIND_CLASS_INFO: {
    cf_constant_t constant_gen = {
        .kind = constant->kind,
        .v =
            {
                .java_class_name = cg_import_constant(
                    dst, src, constant->v.java_class_name, arena),
            },
    };
    return cf_constant_array_push(dst, &constant_gen, arena);
  }
  default:

    pg_assert(0 && "unimplemented");
  }
}

static u16 cg_emit_jump_conditionally(cg_generator_t *gen, u8 jump_opcode,
                                      arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, jump_opcode, arena);
  const u16 jump_from_i = pg_array_len(gen->code->code);
  cf_code_array_push_u8(&gen->code->code, BYTECODE_IMPDEP1, arena);
  cf_code_array_push_u8(&gen->code->code, BYTECODE_IMPDEP2, arena);

  switch (jump_opcode) {
  case BYTECODE_IF_ICMPEQ:
  case BYTECODE_IF_ICMPNE:
  case BYTECODE_IF_ICMPLT:
  case BYTECODE_IF_ICMPGE:
  case BYTECODE_IF_ICMPGT:
  case BYTECODE_IF_ICMPLE:
    cg_frame_stack_pop(gen->frame);
    cg_frame_stack_pop(gen->frame);
    break;
  case BYTECODE_IFEQ:
  case BYTECODE_IFNE:
    cg_frame_stack_pop(gen->frame);
    break;
  default:
    pg_assert(0 && "unreachable/unimplemented");
  }

  return jump_from_i;
}

static u16 cg_emit_jump(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_GOTO, arena);
  const u16 from_location = pg_array_len(gen->code->code);
  cf_code_array_push_u8(&gen->code->code, BYTECODE_IMPDEP1, arena);
  cf_code_array_push_u8(&gen->code->code, BYTECODE_IMPDEP2, arena);

  return from_location;
}

static void cg_emit_pop(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_POP, arena);

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_store_variable_int(cg_generator_t *gen, u8 var_i,
                                       arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) > 0);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_last(gen->frame->stack)->kind == VERIFICATION_INFO_INT);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_ISTORE, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_store_variable_object(cg_generator_t *gen, u8 var_i,
                                          arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) > 0);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_last(gen->frame->stack)->kind == VERIFICATION_INFO_OBJECT);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_ASTORE, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_store_variable_long(cg_generator_t *gen, u8 var_i,
                                        arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) > 0);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_last(gen->frame->stack)->kind == VERIFICATION_INFO_LONG);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LSTORE, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_store_variable(cg_generator_t *gen, u8 var_i,
                                   arena_t *arena) {
  pg_assert(pg_array_len(gen->frame->stack) > 0);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(var_i < gen->frame->locals_physical_count);
  const cf_verification_info_kind_t kind =
      pg_array_last(gen->frame->stack)->kind;

  switch (kind) {
  case VERIFICATION_INFO_INT:
    cg_emit_store_variable_int(gen, var_i, arena);
    break;
  case VERIFICATION_INFO_OBJECT:
    cg_emit_store_variable_object(gen, var_i, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_store_variable_long(gen, var_i, arena);
    break;
  default:
    pg_assert(0 && "unimplemented");
  }
}

static void cg_emit_load_variable_int(cg_generator_t *gen, u8 var_i,
                                      arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_len(gen->frame->locals) > 0);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_ILOAD, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_push(gen->frame,
                      (cf_verification_info_t){.kind = VERIFICATION_INFO_INT},
                      arena);
}

static void cg_emit_load_variable_object(cg_generator_t *gen, u8 var_i,
                                         arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_len(gen->frame->locals) > 0);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_ALOAD, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_push(gen->frame,
                      (cf_verification_info_t){
                          .kind = VERIFICATION_INFO_OBJECT,
                          .extra_data = 0, // FIXME
                      },
                      arena);
}

static void cg_emit_ldc(cg_generator_t *gen,
                        const cf_constant_array_t *constant_pool,
                        u16 constant_i, arena_t *arena) {

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LDC_W, arena);

  const cf_constant_t *const constant =
      cf_constant_array_get(constant_pool, constant_i);
  switch (constant->kind) {
  case CONSTANT_POOL_KIND_INT:
    cf_code_array_push_u16(&gen->code->code, constant_i, arena);
    cg_frame_stack_push(gen->frame,
                        (cf_verification_info_t){.kind = VERIFICATION_INFO_INT},
                        arena);
    break;
  default:
    pg_assert(0 && "unimplemented");
  }
}

static void cg_emit_load_variable_long(cg_generator_t *gen, u8 var_i,
                                       arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_len(gen->frame->locals) > 0);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LLOAD, arena);
  cf_code_array_push_u8(&gen->code->code, var_i, arena);

  cg_frame_stack_push(gen->frame,
                      (cf_verification_info_t){.kind = VERIFICATION_INFO_LONG},
                      arena);
}

static void cg_emit_load_variable(cg_generator_t *gen, u8 var_i,
                                  arena_t *arena) {
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(var_i < gen->frame->locals_physical_count);
  pg_assert(pg_array_len(gen->frame->locals) > 0);

  const cf_verification_info_kind_t kind =
      gen->frame->locals[var_i].verification_info.kind;

  switch (kind) {
  case VERIFICATION_INFO_INT:
    cg_emit_load_variable_int(gen, var_i, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_load_variable_long(gen, var_i, arena);
    break;
  case VERIFICATION_INFO_OBJECT:
    cg_emit_load_variable_object(gen, var_i, arena);
    break;
  default:
    pg_assert(0 && "unimplemented");
  }
}

static void cg_emit_get_static(cg_generator_t *gen, u16 field_i, u16 class_i,
                               arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(field_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);
  pg_assert(arena != NULL);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_GET_STATIC, arena);
  cf_code_array_push_u16(&gen->code->code, field_i, arena);

  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);

  const cf_verification_info_t verification_info = {
      .kind = VERIFICATION_INFO_OBJECT,
      .extra_data = class_i,
  };
  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static void cg_emit_invoke_virtual(cg_generator_t *gen, u16 method_ref_i,
                                   const ty_type_method_t *method_type,
                                   arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(method_ref_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_INVOKE_VIRTUAL, arena);
  cf_code_array_push_u16(&gen->code->code, method_ref_i, arena);

  for (u8 i = 0; i < 1 + pg_array_len(method_type->argument_types_i); i++)
    cg_frame_stack_pop(gen->frame);

  const ty_type_t *const return_type =
      &gen->resolver->types[method_type->return_type_i];

  if (return_type->kind == TYPE_UNIT)
    return;

  const cf_verification_info_t verification_info = cg_type_to_verification_info(
      resolver_eval_type(gen->resolver, return_type));

  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static void cg_emit_invoke_static(cg_generator_t *gen, u16 method_ref_i,
                                  const ty_type_method_t *method_type,
                                  arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(method_ref_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_INVOKE_STATIC, arena);
  cf_code_array_push_u16(&gen->code->code, method_ref_i, arena);

  for (u8 i = 0; i < pg_array_len(method_type->argument_types_i); i++)
    cg_frame_stack_pop(gen->frame);

  const ty_type_t *const return_type =
      &gen->resolver->types[method_type->return_type_i];

  if (return_type->kind == TYPE_UNIT)
    return;

  const cf_verification_info_t verification_info = cg_type_to_verification_info(
      resolver_eval_type(gen->resolver, return_type));

  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static void cg_emit_invoke_special(cg_generator_t *gen, u16 method_ref_i,
                                   const ty_type_method_t *method_type,
                                   arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(method_ref_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_INVOKE_SPECIAL, arena);
  cf_code_array_push_u16(&gen->code->code, method_ref_i, arena);

  for (u8 i = 0; i < pg_array_len(method_type->argument_types_i); i++)
    cg_frame_stack_pop(gen->frame);

  const ty_type_t *const return_type =
      &gen->resolver->types[method_type->return_type_i];

  pg_assert(return_type->kind != TYPE_UNIT);

  const cf_verification_info_t verification_info = cg_type_to_verification_info(
      resolver_eval_type(gen->resolver, return_type));

  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static u32 cg_make_type_from_method_descriptor(cg_generator_t *gen,
                                               cf_class_file_t *class_file,
                                               u16 constant_i, arena_t *arena) {
  const cf_constant_t *const constant =
      cf_constant_array_get(&class_file->constant_pool, constant_i);
  pg_assert(constant->kind == CONSTANT_POOL_KIND_METHOD_REF);

  const cf_constant_t *const name_and_type = cf_constant_array_get(
      &class_file->constant_pool, constant->v.method_ref.name_and_type);
  pg_assert(name_and_type->kind == CONSTANT_POOL_KIND_NAME_AND_TYPE);

  str descriptor = cf_constant_array_get_as_string(
      &class_file->constant_pool, name_and_type->v.name_and_type.descriptor);

  ty_type_t new_type = {0};
  cf_parse_descriptor(gen->resolver, descriptor, &new_type, arena);

  return resolver_add_type(gen->resolver, &new_type, arena);
}

static void cg_emit_inlined_method_call(cg_generator_t *gen,
                                        cf_class_file_t *class_file,
                                        const ty_type_t *method_type,
                                        u16 locals_offset, arena_t *arena) {
  pg_assert(method_type->kind == TYPE_METHOD);
  pg_assert(method_type->flags & TYPE_FLAG_INLINE_ONLY);

  const ty_type_method_t *const method = &method_type->v.method;
  pg_assert(method->code != NULL);
  pg_assert(pg_array_len(method->code) > 0);
  pg_assert(method->constant_pool != NULL);

  const u32 code_size = pg_array_len(method->code);
  u8 *code = method->code;
  u8 *current = code;

  u64 stack_size_before_inline_snippet = pg_array_len(gen->frame->stack);

  while (current < code + code_size) {
    const u8 opcode = buf_read_u8(str_new(code, code_size), &current);

    switch (opcode) {
    case BYTECODE_GET_STATIC: {
      const u16 field_ref_i =
          buf_read_be_u16(str_new(code, code_size), &current);
      const u16 field_ref_gen_i =
          cg_import_constant(&class_file->constant_pool, method->constant_pool,
                             field_ref_i, arena);

      const cf_constant_t *const field_ref_gen =
          cf_constant_array_get(&class_file->constant_pool, field_ref_gen_i);
      pg_assert(field_ref_gen->kind == CONSTANT_POOL_KIND_FIELD_REF);

      cg_emit_get_static(gen, field_ref_gen_i, field_ref_gen->v.field_ref.name,
                         arena);

      break;
    }
    case BYTECODE_INVOKE_VIRTUAL: {
      const u16 method_ref_i =
          buf_read_be_u16(str_new(code, code_size), &current);
      const u16 method_ref_gen_i =
          cg_import_constant(&class_file->constant_pool, method->constant_pool,
                             method_ref_i, arena);

      const u32 invoked_type_i = cg_make_type_from_method_descriptor(
          gen, class_file, method_ref_gen_i, arena);
      const ty_type_t *const invoked_type =
          &gen->resolver->types[invoked_type_i];
      pg_assert(invoked_type->kind == TYPE_METHOD);

      cg_emit_invoke_virtual(gen, method_ref_gen_i, &invoked_type->v.method,
                             arena);

      break;
    }

    case BYTECODE_ISTORE_1: {
      u16 physical_local_index = locals_offset + 1;
      if (physical_local_index >=
          gen->frame->locals_physical_count) { // Need to add the variable.
        const cf_variable_t variable = {
            .scope_depth = gen->frame->scope_depth,
            .verification_info = {.kind = VERIFICATION_INFO_INT},
        };

        u16 logical_local_index = 0;
        cg_frame_locals_push(gen, &variable, &logical_local_index,
                             &physical_local_index, arena);
      }
      cg_emit_store_variable_int(gen, physical_local_index, arena);

      break;
    }

    case BYTECODE_ISTORE_2: {
      u16 physical_local_index = locals_offset + 2;
      if (physical_local_index >=
          gen->frame->locals_physical_count) { // Need to add the variable.
        const cf_variable_t variable = {
            .scope_depth = gen->frame->scope_depth,
            .verification_info = {.kind = VERIFICATION_INFO_INT},
        };

        u16 logical_local_index = 0;
        cg_frame_locals_push(gen, &variable, &logical_local_index,
                             &physical_local_index, arena);
      }
      cg_emit_store_variable_int(gen, physical_local_index, arena);

      break;
    }

    case BYTECODE_ILOAD_0:
      cg_emit_load_variable_int(gen, locals_offset + 0, arena);
      break;

    case BYTECODE_LLOAD_0:
      cg_emit_load_variable_long(gen, locals_offset + 0, arena);
      break;
    case BYTECODE_RETURN:
      // No-op by nature.
      break;

    case BYTECODE_LDC: {
      const u16 constant_i =
          (u16)buf_read_u8(str_new(code, code_size), &current);
      const u16 constant_gen_i = cg_import_constant(
          &class_file->constant_pool, method->constant_pool, constant_i, arena);

      cg_emit_ldc(gen, &class_file->constant_pool, constant_gen_i, arena);

      break;
    }

    case BYTECODE_ALOAD_0: {
      cg_emit_load_variable_object(gen, locals_offset + 0, arena);
      break;
    }
    case BYTECODE_INVOKE_STATIC: {
      const u16 method_ref_i =
          buf_read_be_u16(str_new(code, code_size), &current);
      const u16 method_ref_gen_i =
          cg_import_constant(&class_file->constant_pool, method->constant_pool,
                             method_ref_i, arena);

      const u32 invoked_type_i = cg_make_type_from_method_descriptor(
          gen, class_file, method_ref_gen_i, arena);
      const ty_type_t *const invoked_type =
          &gen->resolver->types[invoked_type_i];
      pg_assert(invoked_type->kind == TYPE_METHOD);

      cg_emit_invoke_static(gen, method_ref_gen_i, &invoked_type->v.method,
                            arena);

      break;
    }

    default:
      pg_assert(0 && "unimplemented");
    }
  }
  u64 stack_size_after_inline_snippet = pg_array_len(gen->frame->stack);

  // Not conceptually required but we do not support inlining snippets that
  // enlarge/shrink the stack right now.
  pg_assert(stack_size_before_inline_snippet ==
            stack_size_after_inline_snippet);
}

static void cg_emit_add(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  pg_assert(pg_array_len(gen->frame->stack) >= 2);

  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_IADD, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LADD, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_neg(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 1);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);
  const cf_verification_info_kind_t kind =
      pg_array_last(gen->frame->stack)->kind;

  switch (kind) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_INEG, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LNEG, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_lcmp(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(arena != NULL);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LCMP, arena);

  cg_frame_stack_pop(gen->frame);
  cg_frame_stack_pop(gen->frame);

  cg_frame_stack_push(gen->frame,
                      (cf_verification_info_t){.kind = VERIFICATION_INFO_INT},
                      arena);
}

static void cg_emit_bipush(cg_generator_t *gen, u8 value, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(arena != NULL);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_BIPUSH, arena);
  cf_code_array_push_u8(&gen->code->code, value, arena);

  cg_frame_stack_push(gen->frame,
                      (cf_verification_info_t){.kind = VERIFICATION_INFO_INT},
                      arena);
}

static void cg_emit_ixor(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);
  pg_assert(gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind ==
            VERIFICATION_INFO_INT);
  pg_assert(gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind ==
            VERIFICATION_INFO_INT);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_IXOR, arena);

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_mul(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_IMUL, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LMUL, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_div(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_IDIV, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LDIV, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }

  cg_frame_stack_pop(gen->frame);
}

static void cg_emit_rem(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_IREM, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LREM, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }

  cg_frame_stack_pop(gen->frame);
}

static void
cg_emit_load_constant_single_word(cg_generator_t *gen, u16 constant_i,
                                  cf_verification_info_t verification_info,
                                  arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(constant_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(cf_verification_info_kind_word_count(verification_info.kind) == 1);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LDC_W, arena);
  cf_code_array_push_u16(&gen->code->code, constant_i, arena);

  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);

  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static void
cg_emit_load_constant_double_word(cg_generator_t *gen, u16 constant_i,
                                  cf_verification_info_t verification_info,
                                  arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(constant_i > 0);
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);
  pg_assert(cf_verification_info_kind_word_count(verification_info.kind) == 2);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_LDC2_W, arena);
  cf_code_array_push_u16(&gen->code->code, constant_i, arena);

  pg_assert(pg_array_len(gen->frame->stack) < UINT16_MAX);

  cg_frame_stack_push(gen->frame, verification_info, arena);
}

static void cg_emit_return_nothing(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(arena != NULL);

  cf_code_array_push_u8(&gen->code->code, BYTECODE_RETURN, arena);
}

static void cg_emit_return_value(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(arena != NULL);

  pg_assert(pg_array_len(gen->frame->stack) >= 1);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);
  const cf_verification_info_kind_t kind =
      pg_array_last(gen->frame->stack)->kind;

  switch (kind) {
  case VERIFICATION_INFO_INT:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_IRETURN, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cf_code_array_push_u8(&gen->code->code, BYTECODE_LRETURN, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_begin_scope(cg_generator_t *gen) {
  pg_assert(gen->frame);
  pg_assert(gen->frame->scope_depth < UINT32_MAX);

  gen->frame->scope_depth += 1;
  gen->scope_id += 1;
}

static void
stack_map_record_frame_at_pc(const cg_frame_t *frame,
                             cf_stack_map_frame_t **stack_map_frames, u16 pc,
                             arena_t *arena) {
  pg_assert(frame != NULL);
  pg_assert(arena != NULL);

  const cf_stack_map_frame_t stack_map_frame = {
      .frame = cg_frame_clone(frame, arena),
      .pc = pc,
  };
  pg_array_append(*stack_map_frames, stack_map_frame, arena);
}

static void cg_frame_drop_current_scope_variables(cg_frame_t *frame) {
  pg_assert(frame != NULL);

  u64 to_drop = 0;
  for (i64 i = pg_array_len(frame->locals) - 1; i >= 0; i--) {
    const cf_variable_t *const variable = &frame->locals[i];
    if (variable->scope_depth < frame->scope_depth)
      break;

    pg_assert(variable->scope_depth == frame->scope_depth);

    to_drop += 1;
  }

  pg_array_drop_last_n(frame->locals, to_drop);
}

static void cg_end_scope(cg_generator_t *gen) {
  pg_assert(gen);
  pg_assert(gen->frame);
  pg_assert(gen->frame->scope_depth > 0);

  cg_frame_drop_current_scope_variables(gen->frame);

  gen->frame->scope_depth -= 1;
}

// TODO: Should the AST_KIND_VAR_DEFINITION node simply store the variable
// slot number? Or use a lookup table?
static bool cf_find_variable(const cg_frame_t *frame, u32 node_i,
                             u16 *logical_local_index,
                             u16 *physical_local_index) {
  pg_assert(frame != NULL);
  pg_assert(frame->locals != NULL);
  pg_assert(node_i > 0);
  pg_assert(logical_local_index != NULL);
  pg_assert(physical_local_index != NULL);

  *physical_local_index = frame->locals_physical_count;
  for (i64 i = pg_array_len(frame->locals) - 1; i >= 0; i--) {
    const cf_variable_t *const variable = &frame->locals[i];
    *physical_local_index -=
        cf_verification_info_kind_word_count(variable->verification_info.kind);
    if (variable->node_i != node_i)
      continue;

    pg_assert(*physical_local_index < frame->locals_physical_count);
    *logical_local_index = i;
    return true;
  }

  return false;
}

static void cg_emit_node(cg_generator_t *gen, cf_class_file_t *class_file,
                         u32 node_i, arena_t *arena);

static void cg_patch_jump_at(cg_generator_t *gen, u16 at, u16 target) {
  pg_assert(gen != NULL);
  pg_assert(gen->code != NULL);
  pg_assert(gen->code->code != NULL);
  pg_assert(at > 0);
  pg_assert(target > 0);

  const i16 jump_offset = target - (at - 1);
  gen->code->code[at + 0] = (u8)(((u16)(jump_offset & 0xff00)) >> 8);
  gen->code->code[at + 1] = (u8)(((u16)(jump_offset & 0x00ff)) >> 0);
}

// TODO: Make a primitive emerge to use here and in cg_emit_if_then_else.
static void cg_emit_synthetic_if_then_else(cg_generator_t *gen,
                                           u8 conditional_jump_opcode,
                                           arena_t *arena) {
  // Assume the condition is already on the stack

  cf_code_array_push_u8(&gen->code->code, conditional_jump_opcode, arena);
  cf_code_array_push_u8(&gen->code->code, 0, arena);
  cf_code_array_push_u8(&gen->code->code, 3 + 2 + 3, arena);

  switch (conditional_jump_opcode) {
  case BYTECODE_IF_ICMPEQ:
  case BYTECODE_IF_ICMPNE:
  case BYTECODE_IF_ICMPLT:
  case BYTECODE_IF_ICMPGE:
  case BYTECODE_IF_ICMPGT:
  case BYTECODE_IF_ICMPLE:
    cg_frame_stack_pop(gen->frame);
    cg_frame_stack_pop(gen->frame);
    break;
  case BYTECODE_IFEQ:
  case BYTECODE_IFNE:
    cg_frame_stack_pop(gen->frame);
    break;
  default:
    pg_assert(0 && "unreachable/unimplemented");
  }

  const cg_frame_t *const frame_before_then_else =
      cg_frame_clone(gen->frame, arena);

  cg_emit_bipush(gen, true, arena); // Then.
  cf_code_array_push_u8(&gen->code->code, BYTECODE_GOTO, arena);
  cf_code_array_push_u8(&gen->code->code, 0, arena);
  cf_code_array_push_u8(&gen->code->code, 3 + 2, arena);

  const cg_frame_t *const frame_after_then = cg_frame_clone(gen->frame, arena);

  gen->frame = cg_frame_clone(frame_before_then_else, arena);

  const u16 conditional_jump_target_absolute = pg_array_len(gen->code->code);
  cg_emit_bipush(gen, false, arena); // Else.

  const u16 unconditional_jump_target_absolute = pg_array_len(gen->code->code);

  stack_map_record_frame_at_pc(frame_before_then_else, &gen->stack_map_frames,
                               conditional_jump_target_absolute, arena);
  stack_map_record_frame_at_pc(frame_after_then, &gen->stack_map_frames,
                               unconditional_jump_target_absolute, arena);
}

static void cg_emit_gt(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPLE, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_bipush(gen, 1, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPNE, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_ne(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPEQ, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IFEQ, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_eq(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPNE, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IFNE, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_ge(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPLT, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_bipush(gen, -1, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPEQ, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_le(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPGT, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_bipush(gen, 1, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPEQ, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_lt(cg_generator_t *gen, arena_t *arena) {
  pg_assert(gen->frame != NULL);
  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  pg_assert(pg_array_len(gen->frame->stack) <= UINT16_MAX);

  pg_assert(pg_array_len(gen->frame->stack) >= 2);
  const cf_verification_info_kind_t kind_a =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 1].kind;
  const cf_verification_info_kind_t kind_b =
      gen->frame->stack[pg_array_len(gen->frame->stack) - 2].kind;

  pg_assert(kind_a == kind_b);

  switch (kind_a) {
  case VERIFICATION_INFO_INT:
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPGE, arena);
    break;
  case VERIFICATION_INFO_LONG:
    cg_emit_lcmp(gen, arena);
    cg_emit_bipush(gen, -1, arena);
    cg_emit_synthetic_if_then_else(gen, BYTECODE_IF_ICMPNE, arena);
    break;
  default:
    pg_assert(0 && "todo");
  }
}

static void cg_emit_if_then_else(cg_generator_t *gen,
                                 cf_class_file_t *class_file, u32 node_i,
                                 arena_t *arena) {
  // clang-format off
//
//               IF 
//              /  \
//    condition     THEN_ELSE
//                 /      \
//             then        else
//
//                 <condition expression>
//      x     ---- jump_conditionally (IFEQ,  etc)
//      x     |    jump_conditionally_offset1
//      x     |    jump_conditionally_offset2
//      x     |    <then branch>
//  +   x  ...|... jump
//  +   x  .  |    jump_offset1 
//  +   x  .  |    jump_offset2
//  +   x  .  |--> <else branch> 
//  +      ......> ...          
//
  // clang-format on

  pg_assert(gen != NULL);
  pg_assert(gen->resolver->parser != NULL);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);
  pg_assert(node_i < pg_array_len(gen->resolver->parser->nodes));
  pg_assert(gen->frame != NULL);
  pg_assert(gen->frame->locals != NULL);
  pg_assert(gen->frame->stack != NULL);
  pg_assert(gen->stack_map_frames != NULL);

  const par_ast_node_t *const node = &gen->resolver->parser->nodes[node_i];
  pg_assert(node->type_i > 0);
  pg_assert(node->lhs > 0);
  pg_assert(node->lhs < pg_array_len(gen->resolver->parser->nodes));
  pg_assert(node->rhs > 0);
  pg_assert(node->rhs < pg_array_len(gen->resolver->parser->nodes));

  // Emit condition.
  cg_emit_node(gen, class_file, node->lhs, arena);

  const u16 jump_conditionally_from_i =
      cg_emit_jump_conditionally(gen, BYTECODE_IFEQ, arena);

  pg_assert(node->rhs > 0);
  const par_ast_node_t *const rhs = &gen->resolver->parser->nodes[node->rhs];
  pg_assert(rhs->kind == AST_KIND_THEN_ELSE);

  // Emit `then` branch.
  // Save a clone of the frame before the `then` branch since we need it
  // later.
  const cg_frame_t *const frame_before_then_else =
      cg_frame_clone(gen->frame, arena);

  cg_emit_node(gen, class_file, rhs->lhs, arena);
  const u16 jump_from_i = (rhs->rhs > 0) ? cg_emit_jump(gen, arena) : 0;
  const u16 conditional_jump_target_absolute = pg_array_len(gen->code->code);

  // Save a clone of the frame after the `then` branch executed so that we
  // can generate a stack map frame later.
  const cg_frame_t *const frame_after_then = cg_frame_clone(gen->frame, arena);

  // Emit `else` branch.
  // Restore the frame as if the `then` branch never executed.
  gen->frame = cg_frame_clone(frame_before_then_else, arena);

  cg_emit_node(gen, class_file, rhs->rhs, arena);
  const u16 unconditional_jump_target_absolute = pg_array_len(gen->code->code);

  gen->frame->max_physical_stack = pg_max(frame_after_then->max_physical_stack,
                                          gen->frame->max_physical_stack);
  gen->frame->max_physical_locals = pg_max(
      frame_after_then->max_physical_locals, gen->frame->max_physical_locals);
  // TODO: assert that the stack/locals count is the same?

  // Patch first, conditional, jump.
  {
    cg_patch_jump_at(gen, jump_conditionally_from_i,
                     conditional_jump_target_absolute);
    stack_map_record_frame_at_pc(frame_before_then_else, &gen->stack_map_frames,
                                 conditional_jump_target_absolute, arena);
  }
  // Patch second, unconditional, jump.
  {
    if (rhs->rhs > 0) {
      cg_patch_jump_at(gen, jump_from_i, unconditional_jump_target_absolute);

      stack_map_record_frame_at_pc(frame_after_then, &gen->stack_map_frames,
                                   unconditional_jump_target_absolute, arena);
    }
  }
}

static int stack_map_frame_sort(const void *a, const void *b) {
  pg_assert(a != NULL);
  pg_assert(b != NULL);

  const cf_stack_map_frame_t *const smp_a = a;
  const cf_stack_map_frame_t *const smp_b = b;

  return (int)smp_a->pc - (int)smp_b->pc;
}

static void stack_map_resolve_frames(const cg_frame_t *first_method_frame,
                                     cf_stack_map_frame_t *stack_map_frames,
                                     arena_t *arena) {
  pg_assert(first_method_frame != NULL);
  pg_assert(stack_map_frames != NULL);
  pg_assert(arena != NULL);

  if (pg_array_len(stack_map_frames) == 0)
    return;

  // TODO: Better sort.
  qsort(stack_map_frames, pg_array_len(stack_map_frames),
        sizeof(cf_stack_map_frame_t), stack_map_frame_sort);

  for (u64 i = 0; i < pg_array_len(stack_map_frames); i++) {
    cf_stack_map_frame_t *const stack_map_frame = &stack_map_frames[i];
    cg_frame_t *const frame = stack_map_frame->frame;

    const cg_frame_t *const previous_frame =
        i > 0 ? stack_map_frames[i - 1].frame : first_method_frame;

    i16 locals_delta =
        pg_array_len(frame->locals) - pg_array_len(previous_frame->locals);

    i32 offset_delta =
        i == 0 ? stack_map_frame->pc
               : (stack_map_frame->pc - stack_map_frames[i - 1].pc - 1);

    if (offset_delta == -1) // Duplicate jump target, already has a valid
                            // stack map frame, skip.
    {
      stack_map_frame->tombstone = true;
      continue;
    }

    pg_assert(offset_delta >= 0);
    pg_assert(offset_delta <= UINT16_MAX);

    if (frame->stack_physical_count == 0 && locals_delta == 0 &&
        offset_delta <= 63) {
      stack_map_frame->kind = offset_delta;
      stack_map_frame->offset_delta = offset_delta;
    } else if (frame->stack_physical_count == 0 && locals_delta == 0 &&
               offset_delta > 63) {
      pg_assert(0 && "todo"); // same_frame_extended
    } else if (frame->stack_physical_count == 1 && locals_delta == 0 &&
               offset_delta <= 63) {
      stack_map_frame->kind = offset_delta + 64;
      stack_map_frame->offset_delta = offset_delta;

      pg_assert(stack_map_frame->kind >= 64);
      pg_assert(stack_map_frame->kind <= 127);
    } else if (frame->stack_physical_count == 1 && locals_delta == 0 &&
               offset_delta <= 63) {
      pg_assert(0 && "todo"); // same_locals_1_stack_item_frame_extended
    } else if (frame->stack_physical_count == 0 &&
               (1 <= locals_delta && locals_delta <= 3)) { // append_frame
      stack_map_frame->kind = 251 + locals_delta;
      stack_map_frame->offset_delta = offset_delta;
    } else if (frame->stack_physical_count == 0 &&
               (locals_delta == -1 || locals_delta == -2 ||
                locals_delta == -3) &&
               offset_delta <= 3) {
      pg_assert(0 && "todo"); // chop_frame
    } else {
      stack_map_frame->kind = 255;
      stack_map_frame->offset_delta = offset_delta;
    }
  }
}

__attribute__((unused)) static u16
cg_add_class_name_in_constant_pool(cf_class_file_t *class_file, str class_name,
                                   arena_t *arena) {
  const u16 class_name_i =
      cf_add_constant_string(&class_file->constant_pool, class_name, arena);
  const cf_constant_t out_class = {.kind = CONSTANT_POOL_KIND_CLASS_INFO,
                                   .v = {.java_class_name = class_name_i}};
  const u16 class_i =
      cf_constant_array_push(&class_file->constant_pool, &out_class, arena);

  return class_i;
}

static void cg_emit_node(cg_generator_t *gen, cf_class_file_t *class_file,
                         u32 node_i, arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->resolver->parser != NULL);
  pg_assert(gen->resolver->parser->lexer != NULL);
  pg_assert(gen->resolver->parser->lexer->tokens != NULL);
  pg_assert(gen->resolver->parser->nodes != NULL);
  pg_assert(gen->resolver->parser->tokens_i <=
            pg_array_len(gen->resolver->parser->lexer->tokens));
  pg_assert(pg_array_len(gen->resolver->parser->nodes) > 0);
  pg_assert(class_file != NULL);
  pg_assert(node_i < pg_array_len(gen->resolver->parser->nodes));

  const par_ast_node_t *const node = &gen->resolver->parser->nodes[node_i];
  const lex_token_t token =
      gen->resolver->parser->lexer->tokens[node->main_token_i];
  const ty_type_t *const type = &gen->resolver->types[node->type_i];

  switch (node->kind) {
  case AST_KIND_NONE:
    return;
  case AST_KIND_BOOL: {
    pg_assert(node->main_token_i <
              pg_array_len(gen->resolver->parser->lexer->tokens));
    const cf_constant_t constant = {.kind = CONSTANT_POOL_KIND_INT,
                                    .v = {.number = node->lhs}};
    const u16 number_i =
        cf_constant_array_push(&class_file->constant_pool, &constant, arena);

    pg_assert(gen->code != NULL);
    pg_assert(gen->code->code != NULL);
    pg_assert(gen->frame != NULL);

    const cf_verification_info_t verification_info = {
        .kind = VERIFICATION_INFO_INT};
    cg_emit_load_constant_single_word(gen, number_i, verification_info, arena);
    break;
  }
  case AST_KIND_NUMBER: {
    pg_assert(node->main_token_i <
              pg_array_len(gen->resolver->parser->lexer->tokens));

    cp_info_kind_t pool_kind = CONSTANT_POOL_KIND_INT;
    cf_verification_info_kind_t verification_info_kind = VERIFICATION_INFO_INT;
    if (type->kind == TYPE_LONG) {
      pool_kind = CONSTANT_POOL_KIND_LONG;
      verification_info_kind = VERIFICATION_INFO_LONG;
    }
    // TODO: Float, Double, etc.

    const u64 number = node->extra_data_i;
    const cf_constant_t constant = {.kind = pool_kind, .v = {.number = number}};
    const u16 number_i =
        cf_constant_array_push(&class_file->constant_pool, &constant, arena);
    if (pool_kind == CONSTANT_POOL_KIND_LONG ||
        pool_kind == CONSTANT_POOL_KIND_DOUBLE) {
      const cf_constant_t dummy = {0};
      cf_constant_array_push(&class_file->constant_pool, &dummy, arena);

      cg_emit_load_constant_double_word(
          gen, number_i,
          (cf_verification_info_t){.kind = verification_info_kind}, arena);
    } else {
      cg_emit_load_constant_single_word(
          gen, number_i,
          (cf_verification_info_t){.kind = verification_info_kind}, arena);
    }
    break;
  }
  case AST_KIND_CALL: {
    pg_assert(type->this_class_name.data != NULL);
    pg_assert(type->this_class_name.len > 0);
    pg_assert(type->kind == TYPE_METHOD || type->kind == TYPE_CONSTRUCTOR);

    for (u64 i = 0; i < pg_array_len(node->nodes); i++) {
      cg_emit_node(gen, class_file, node->nodes[i], arena);
    }

    if (type->flags & TYPE_FLAG_INLINE_ONLY) {
      const u16 initial_locals_physical_count =
          gen->frame->locals_physical_count;

      u64 stack_index = pg_array_last_index(gen->frame->stack);

      for (u64 i = 0; i < pg_array_len(node->nodes); i++) {
        const u32 argument_i = node->nodes[i];
        const par_ast_node_t *const argument =
            &gen->resolver->parser->nodes[argument_i];

        // Each argument `a, b, c` is now on the stack in order: `[..] [this] a
        // b c` with the corresponding verification info.
        const cf_verification_info_t verification_info =
            gen->frame->stack[stack_index];
        const cf_variable_t variable = {
            .node_i = argument_i,
            .type_i = argument->type_i,
            .scope_depth = gen->frame->scope_depth,
            .verification_info = verification_info,
        };

        u16 logical_local_index = 0;
        u16 physical_local_index = 0;
        cg_frame_locals_push(gen, &variable, &logical_local_index,
                             &physical_local_index, arena);

        cg_emit_store_variable(gen, physical_local_index, arena);

        stack_index -=
            cf_verification_info_kind_word_count(verification_info.kind);
      }
      cg_emit_inlined_method_call(gen, class_file, type,
                                  initial_locals_physical_count, arena);

    } else {
      // TODO: Support non static calls.
      pg_assert(type->v.method.access_flags & ACCESS_FLAGS_STATIC);

      const cf_constant_t class_name = {
          .kind = CONSTANT_POOL_KIND_UTF8,
          .v = {.s = gen->resolver->this_class_name}};
      const u16 class_name_i = cf_constant_array_push(
          &class_file->constant_pool, &class_name, arena);

      const cf_constant_t class = {.kind = CONSTANT_POOL_KIND_CLASS_INFO,
                                   .v = {.java_class_name = class_name_i}};
      const u16 class_i =
          cf_constant_array_push(&class_file->constant_pool, &class, arena);

      const cf_constant_t name = {
          .kind = CONSTANT_POOL_KIND_UTF8,
          .v = {
              .s = type->kind == TYPE_METHOD ? type->v.method.name
                                             : str_from_c(CONSTRUCTOR_JVM_NAME),
          }};
      const u16 name_i =
          cf_constant_array_push(&class_file->constant_pool, &name, arena);

      str_builder descriptor_s = sb_new(256, arena);
      descriptor_s = cf_fill_descriptor_string(
          gen->resolver->types, descriptor_s, node->type_i, arena);
      const cf_constant_t descriptor = {.kind = CONSTANT_POOL_KIND_UTF8,
                                        .v = {.s = sb_build(descriptor_s)}};
      const u16 descriptor_i = cf_constant_array_push(
          &class_file->constant_pool, &descriptor, arena);

      const cf_constant_t name_and_type = {
          .kind = CONSTANT_POOL_KIND_NAME_AND_TYPE,
          .v = {.name_and_type = {.name = name_i, .descriptor = descriptor_i}}};
      const u16 name_and_type_i = cf_constant_array_push(
          &class_file->constant_pool, &name_and_type, arena);

      cf_constant_t method_ref = {
          .kind = CONSTANT_POOL_KIND_METHOD_REF,
          .v = {.method_ref = {.class = class_i,
                               .name_and_type = name_and_type_i}}};
      const u16 method_ref_i = cf_constant_array_push(
          &class_file->constant_pool, &method_ref, arena);

      if (type->kind == TYPE_METHOD)
        cg_emit_invoke_static(gen, method_ref_i, &type->v.method, arena);
      else if (type->kind == TYPE_CONSTRUCTOR)
        cg_emit_invoke_special(gen, method_ref_i, &type->v.method, arena);
    }

    break;
  }
  case AST_KIND_FUNCTION_DEFINITION: {
    pg_array_clear(gen->locals);

    const u32 token_name_i = node->main_token_i;
    pg_assert(token_name_i <
              pg_array_len(gen->resolver->parser->lexer->tokens));
    const lex_token_t token_name =
        gen->resolver->parser->lexer->tokens[token_name_i];
    pg_assert(token_name.kind == TOKEN_KIND_IDENTIFIER);

    str method_name = {
        .len = lex_identifier_length(gen->resolver->parser->buf,
                                     token_name.source_offset),
        .data = &gen->resolver->parser->buf.data[token_name.source_offset],
    };
    const u16 method_name_i =
        cf_add_constant_string(&class_file->constant_pool, method_name, arena);

    str_builder descriptor = sb_new(64, arena);
    descriptor = cf_fill_descriptor_string(gen->resolver->types, descriptor,
                                           node->type_i, arena);
    const u16 descriptor_i = cf_add_constant_string(
        &class_file->constant_pool, sb_build(descriptor), arena);

    cf_method_t method = {
        .access_flags = ACCESS_FLAGS_STATIC | ACCESS_FLAGS_PUBLIC,
        .name = method_name_i,
        .descriptor = descriptor_i,
    };
    pg_array_init_reserve(method.attributes, 1, arena);

    cf_attribute_code_t code = {0};
    cf_attribute_code_init(&code, arena);
    gen->code = &code;
    cg_frame_t frame = {0};
    cg_frame_init(&frame, arena);

    gen->frame = &frame;

    // `lhs` is the arguments, `rhs` is the body.

    cg_begin_scope(gen);
    // Emit parameters i.e. locals.
    cg_emit_node(gen, class_file, node->lhs, arena);
    // The firt frame implicitly encompasses arguments as locals.
    cg_frame_t *const first_method_frame = cg_frame_clone(gen->frame, arena);
    // Emit the body.
    cg_emit_node(gen, class_file, node->rhs, arena);

    { // Add return if there is none e.g. the function body is empty or the
      // return type is Unit.
      // TODO: Should it be in the lowering phase instead?

      if (node->rhs == 0) { // Empty body
        cg_emit_return_nothing(gen, arena);
      } else {
        const par_ast_node_t *const rhs =
            &gen->resolver->parser->nodes[node->rhs];
        pg_assert(rhs->kind == AST_KIND_LIST);

        if (pg_array_len(rhs->nodes) == 0) {
          cg_emit_return_nothing(gen, arena);
        } else {
          const u32 last_node_i = rhs->nodes[pg_array_len(rhs->nodes) - 1];
          const par_ast_node_t *const last_node =
              &gen->resolver->parser->nodes[last_node_i];

          if (last_node->kind != AST_KIND_RETURN) {
            cg_emit_return_nothing(gen, arena);
          }
        }
      }
    }

    cg_end_scope(gen);

    gen->code->max_physical_stack = gen->frame->max_physical_stack;
    gen->code->max_physical_locals = gen->frame->max_physical_locals;

    stack_map_resolve_frames(first_method_frame, gen->stack_map_frames, arena);

    cf_attribute_t attribute_stack_map_frames = {
        .kind = ATTRIBUTE_KIND_STACK_MAP_TABLE,
        .name = cf_add_constant_cstring(&class_file->constant_pool,
                                        "StackMapTable", arena),
        .v = {.stack_map_table = NULL}};
    pg_array_init_reserve(attribute_stack_map_frames.v.stack_map_table,
                          pg_array_len(gen->stack_map_frames), arena);

    for (u64 i = 0; i < pg_array_len(gen->stack_map_frames); i++) {
      if (!gen->stack_map_frames[i].tombstone)
        pg_array_append(attribute_stack_map_frames.v.stack_map_table,
                        gen->stack_map_frames[i], arena);
    }

    pg_array_append(code.attributes, attribute_stack_map_frames, arena);

    const cf_attribute_t attribute_code = {
        .kind = ATTRIBUTE_KIND_CODE,
        .name =
            cf_add_constant_cstring(&class_file->constant_pool, "Code", arena),
        .v = {.code = code}};
    pg_array_append(method.attributes, attribute_code, arena);

    pg_array_append(class_file->methods, method, arena);

    gen->code = NULL;
    gen->frame = NULL;
    pg_array_clear(gen->stack_map_frames);
    break;
  }
  case AST_KIND_UNARY: {

    switch (token.kind) {
    case TOKEN_KIND_NOT:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_bipush(gen, 1, arena);
      cg_emit_ixor(gen, arena);
      break;

    case TOKEN_KIND_MINUS:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_neg(gen, arena);
      break;

    default:
      pg_assert(0 && "unimplemented");
    }
    break;
  }
  case AST_KIND_BINARY: {
    pg_assert(gen->frame != NULL);
    pg_assert(gen->frame->locals != NULL);

    pg_assert(node->lhs < pg_array_len(gen->resolver->parser->nodes));
    pg_assert(node->rhs < pg_array_len(gen->resolver->parser->nodes));

    switch (token.kind) {
    case TOKEN_KIND_NONE:
      break; // Nothing to do.

    case TOKEN_KIND_PLUS:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_add(gen, arena);
      break;

    case TOKEN_KIND_MINUS:
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_neg(gen, arena);
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_add(gen, arena);
      break;

    case TOKEN_KIND_STAR:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_mul(gen, arena);
      break;

    case TOKEN_KIND_SLASH:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_div(gen, arena);
      break;

    case TOKEN_KIND_PERCENT:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_rem(gen, arena);
      break;

    case TOKEN_KIND_AMPERSAND_AMPERSAND: {
      // Since the if_xxx opcodes always pop the condition off the stack,
      // there is no simple way to push 0 on the stack if `lhs` is falsey.
      // We have to use this contrived way, short of advanced CFG analysis.
      // :(
      //
      // clang-format off
      //
      // a && b
      // 
      // <=>
      // 
      // if (a) {
      //   if (b) {
      //     push 1 
      //     goto end
      //   }  
      // } else {
      //   push 0
      // }
      // end:
      //
      //                 lhs
      //      x     ---- jump_conditionally (IFEQ,  etc)
      //      x     |    jump_conditionally_offset1 
      //      x     |    jump_conditionally_offset2
      //      x     |    rhs
      //  +   x  ...|... jump
      //  +   x  .  |    jump_offset1 
      //  +   x  .  |    jump_offset2
      //  +   x  .  |--> bipush 0
      //  +      ......> ...           
      //
      // clang-format on
      cg_emit_node(gen, class_file, node->lhs, arena);

      cf_code_array_push_u8(&gen->code->code, BYTECODE_IFEQ, arena);
      const u16 conditional_jump_location = pg_array_len(gen->code->code);
      cf_code_array_push_u16(&gen->code->code, 0, arena);
      cg_frame_stack_pop(gen->frame);

      const cg_frame_t *const frame_before_rhs =
          cg_frame_clone(gen->frame, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);

      const cg_frame_t *const frame_after_rhs =
          cg_frame_clone(gen->frame, arena);
      const u16 unconditional_jump_location = cg_emit_jump(gen, arena);

      {
        const u16 pc_end = pg_array_len(gen->code->code);
        cg_patch_jump_at(gen, conditional_jump_location, pc_end);
        stack_map_record_frame_at_pc(frame_before_rhs, &gen->stack_map_frames,
                                     pc_end, arena);
      }

      // Restore the frame as if the `rhs` branch never executed.
      gen->frame = cg_frame_clone(frame_before_rhs, arena);
      cg_emit_bipush(gen, false, arena);

      {
        const u16 pc_end = pg_array_len(gen->code->code);
        cg_patch_jump_at(gen, unconditional_jump_location, pc_end);
        stack_map_record_frame_at_pc(frame_after_rhs, &gen->stack_map_frames,
                                     pc_end, arena);
      }

      break;
    }

    case TOKEN_KIND_PIPE_PIPE: {
      // Since the if_xxx opcodes always pop the condition off the stack,
      // there is no simple way to push 0 on the stack if `lhs` is falsey.
      // We have to use this contrived way, short of advanced CFG analysis.
      // :(
      //
      // clang-format off
      //
      // a || b
      // 
      // <=>
      // 
      // if (a) {
      //   push 1
      // } else {
      //   if (b) {
      //     push 1 
      //     goto end
      //   }
      //   push 0
      // }
      // end:
      //
      //                 lhs
      //      x     ---- jump_conditionally (IFNE)
      //      x     |    jump_conditionally_offset1 
      //      x     |    jump_conditionally_offset2
      //      x     |    rhs
      //  +   x  ...|... jump
      //  +   x  .  |    jump_offset1 
      //  +   x  .  |    jump_offset2
      //  +   x  .  |--> bipush 1
      //  +      ......> ...           
      //
      // clang-format on
      cg_emit_node(gen, class_file, node->lhs, arena);

      cf_code_array_push_u8(&gen->code->code, BYTECODE_IFNE, arena);
      const u16 conditional_jump_location = pg_array_len(gen->code->code);
      cf_code_array_push_u16(&gen->code->code, 0, arena);
      cg_frame_stack_pop(gen->frame);

      const cg_frame_t *const frame_before_rhs =
          cg_frame_clone(gen->frame, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);

      const cg_frame_t *const frame_after_rhs =
          cg_frame_clone(gen->frame, arena);
      const u16 unconditional_jump_location = cg_emit_jump(gen, arena);

      {
        const u16 pc_end = pg_array_len(gen->code->code);
        cg_patch_jump_at(gen, conditional_jump_location, pc_end);
        stack_map_record_frame_at_pc(frame_before_rhs, &gen->stack_map_frames,
                                     pc_end, arena);
      }

      // Restore the frame as if the `rhs` branch never executed.
      gen->frame = cg_frame_clone(frame_before_rhs, arena);
      cg_emit_bipush(gen, true, arena);

      {
        const u16 pc_end = pg_array_len(gen->code->code);
        cg_patch_jump_at(gen, unconditional_jump_location, pc_end);
        stack_map_record_frame_at_pc(frame_after_rhs, &gen->stack_map_frames,
                                     pc_end, arena);
      }

      break;
    }

    case TOKEN_KIND_EQUAL_EQUAL:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_eq(gen, arena);
      break;

    case TOKEN_KIND_LE:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_le(gen, arena);
      break;

    case TOKEN_KIND_LT:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_lt(gen, arena);
      break;

    case TOKEN_KIND_GT:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_gt(gen, arena);
      break;

    case TOKEN_KIND_GE:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_ge(gen, arena);
      break;

    case TOKEN_KIND_NOT_EQUAL:
      cg_emit_node(gen, class_file, node->lhs, arena);
      cg_emit_node(gen, class_file, node->rhs, arena);
      cg_emit_ne(gen, arena);
      break;

    default:
      pg_assert(0 && "todo");
    }
    break;
  }
  case AST_KIND_LIST: {
    if (gen->code != NULL) {
      pg_assert(gen->frame != NULL);
      pg_assert(gen->frame->locals != NULL);
    }

    for (u64 i = 0; i < pg_array_len(node->nodes); i++) {
      const u32 child_i = node->nodes[i];

      if (gen->frame != NULL) {
        pg_assert(pg_array_len(gen->frame->stack) == 0);
      }
      cg_emit_node(gen, class_file, child_i, arena);

      // If the 'statement' was in fact an expression, we need to pop it
      // out.
      // IMPROVEMENT: If we emit the pop earlier, some stack map frames
      // don't have to be a full_frame but can be something smaller e.g.
      // append_frame.
      const par_ast_node_t *const child =
          &gen->resolver->parser->nodes[child_i];
      if (child->kind != AST_KIND_RETURN && // Avoid: `return; pop;`
          gen->frame != NULL) {
        while (pg_array_len(gen->frame->stack) > 0)
          cg_emit_pop(gen, arena);
      }
    }

    break;
  }
  case AST_KIND_VAR_DEFINITION: {
    pg_assert(gen->frame != NULL);
    pg_assert(gen->frame->locals != NULL);
    pg_assert(node->type_i > 0);

    cg_emit_node(gen, class_file, node->lhs, arena);
    cg_emit_node(gen, class_file, node->rhs, arena);

    const cf_verification_info_t verification_info =
        cg_type_to_verification_info(resolver_eval_type(gen->resolver, type));
    const cf_variable_t variable = {
        .node_i = node_i,
        .type_i = node->type_i,
        .scope_depth = gen->frame->scope_depth,
        .verification_info = verification_info,
    };

    u16 logical_local_index = 0;
    u16 physical_local_index = 0;
    cg_frame_locals_push(gen, &variable, &logical_local_index,
                         &physical_local_index, arena);

    cg_emit_store_variable(gen, physical_local_index, arena);
    break;
  }
  case AST_KIND_VAR_REFERENCE: {
    pg_assert(gen->frame != NULL);
    pg_assert(gen->frame->locals != NULL);
    pg_assert(node->type_i > 0);

    pg_assert(node->lhs > 0);
    pg_assert(gen->resolver->parser->nodes[node->lhs].kind ==
                  AST_KIND_VAR_DEFINITION ||
              gen->resolver->parser->nodes[node->lhs].kind ==
                  AST_KIND_FUNCTION_PARAMETER);

    u16 logical_local_index = 0;
    u16 physical_local_index = 0;
    pg_assert(cf_find_variable(gen->frame, node->lhs, &logical_local_index,
                               &physical_local_index));

    const cf_verification_info_t verification_info =
        gen->frame->locals[logical_local_index].verification_info;
    if (verification_info.kind == VERIFICATION_INFO_INT)
      cg_emit_load_variable_int(gen, physical_local_index, arena);
    else if (verification_info.kind == VERIFICATION_INFO_LONG)
      cg_emit_load_variable_long(gen, physical_local_index, arena);
    else
      pg_assert(0 && "todo");

    break;
  }
  case AST_KIND_IF: {
    cg_emit_if_then_else(gen, class_file, node_i, arena);
    break;
  }
  case AST_KIND_WHILE_LOOP: {
    const u16 pc_start = pg_array_len(gen->code->code);
    const cg_frame_t *const frame_before_loop =
        cg_frame_clone(gen->frame, arena);

    cg_emit_node(gen, class_file, node->lhs, arena); // Condition.
    const u16 conditional_jump =
        cg_emit_jump_conditionally(gen, BYTECODE_IFEQ, arena);
    cg_emit_node(gen, class_file, node->rhs, arena); // Body.
    const u16 unconditional_jump = cg_emit_jump(gen, arena);

    const i16 unconditional_jump_delta = -(unconditional_jump - 1 - pc_start);
    gen->code->code[unconditional_jump + 0] =
        (u8)(((u16)(unconditional_jump_delta & 0xff00)) >> 8);
    gen->code->code[unconditional_jump + 1] =
        (u8)(((u16)(unconditional_jump_delta & 0x00ff)) >> 0);

    const u16 pc_end = pg_array_len(gen->code->code);

    // This stack map frame covers the unconditional jump.
    stack_map_record_frame_at_pc(frame_before_loop, &gen->stack_map_frames,
                                 pc_start, arena);

    // Patch first, conditional, jump.
    {
      cg_patch_jump_at(gen, conditional_jump, pc_end);
      stack_map_record_frame_at_pc(frame_before_loop, &gen->stack_map_frames,
                                   pc_end, arena);
    }

    break;
  }
  case AST_KIND_STRING: {
    const u32 length =
        lex_string_length(gen->resolver->parser->buf, token.source_offset);
    str s = {
        .data = gen->resolver->parser->buf.data + token.source_offset,
        .len = length,
    };
    const u16 string_i =
        cf_add_constant_string(&class_file->constant_pool, s, arena);
    const u16 jstring_i =
        cf_add_constant_jstring(&class_file->constant_pool, string_i, arena);

    // TODO: Deduplicate.
    const cf_constant_t string_class_info = {
        .kind = CONSTANT_POOL_KIND_CLASS_INFO,
        .v = {
            .java_class_name =
                cf_add_constant_string(&class_file->constant_pool,
                                       str_from_c("java/lang/String"), arena),
        }};

    const cf_verification_info_t verification_info = {
        .kind = VERIFICATION_INFO_OBJECT,
        .extra_data = cf_constant_array_push(&class_file->constant_pool,
                                             &string_class_info, arena),
    };
    cg_emit_load_constant_single_word(gen, jstring_i, verification_info, arena);

    break;
  }
  case AST_KIND_CLASS_REFERENCE: {
    pg_assert(0 && "todo");
    break;
  }
  case AST_KIND_NAVIGATION: {
    pg_assert(0 && "todo");
    break;
  }
  case AST_KIND_FUNCTION_PARAMETER: {
    const cf_verification_info_t verification_info =
        cg_type_to_verification_info(resolver_eval_type(gen->resolver, type));
    const cf_variable_t argument = {
        .node_i = node_i,
        .type_i = node->type_i,
        .scope_depth = gen->frame->scope_depth,
        .verification_info = verification_info,
    };
    u16 logical_local_index = 0;
    u16 physical_local_index = 0;
    cg_frame_locals_push(gen, &argument, &logical_local_index,
                         &physical_local_index, arena);
    break;
  }

  case AST_KIND_TYPE:
    // No-op. Although at some point we might need to generate RTTI or such.
    return;

  case AST_KIND_THEN_ELSE:
  case AST_KIND_UNRESOLVED_NAME:
    pg_assert(0 && "unreachable");

  case AST_KIND_ASSIGNMENT:
    pg_assert(node->lhs > 0);
    const par_ast_node_t *const lhs = &gen->resolver->parser->nodes[node->lhs];
    pg_assert(lhs->kind == AST_KIND_VAR_REFERENCE);

    cg_emit_node(gen, class_file, node->rhs, arena);

    u16 logical_local_index = 0;
    u16 physical_local_index = 0;
    pg_assert(cf_find_variable(gen->frame, lhs->lhs, &logical_local_index,
                               &physical_local_index));

    cg_emit_store_variable(gen, physical_local_index, arena);
    break;

  case AST_KIND_RETURN:
    cg_emit_node(gen, class_file, node->lhs, arena);
    type->kind == TYPE_UNIT ? cg_emit_return_nothing(gen, arena)
                            : cg_emit_return_value(gen, arena);
    break;

  case AST_KIND_MAX:
    pg_assert(0 && "unreachable");
  }
}

static str cg_make_class_name_from_path(str path, arena_t *arena) {

  str_split_result_t slash_split = str_rsplit(path, '/');
  pg_assert(!str_is_empty(slash_split.right));

  str_split_result_t dot_split = str_rsplit(slash_split.right, '.');
  pg_assert(dot_split.found);
  pg_assert(!str_is_empty(dot_split.left));

  str_builder res = sb_clone(dot_split.left, arena);
  res = sb_capitalize_at(res, 0);
  return sb_build(res);
}

static void cg_emit_synthetic_class(cg_generator_t *gen,
                                    cf_class_file_t *class_file,
                                    arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->resolver != NULL);
  pg_assert(gen->resolver->parser != NULL);
  pg_assert(gen->resolver->parser->lexer != NULL);
  pg_assert(gen->resolver->parser->lexer->tokens != NULL);
  pg_assert(gen->resolver->parser->nodes != NULL);
  pg_assert(gen->resolver->parser->tokens_i <=
            pg_array_len(gen->resolver->parser->lexer->tokens));
  pg_assert(pg_array_len(gen->resolver->parser->nodes) > 0);
  pg_assert(class_file != NULL);
  pg_assert(arena != NULL);
  pg_assert(!str_is_empty(gen->resolver->this_class_name));

  { // This class
    const u16 this_class_name_i = cf_add_constant_string(
        &class_file->constant_pool, gen->resolver->this_class_name, arena);

    const cf_constant_t this_class_info = {
        .kind = CONSTANT_POOL_KIND_CLASS_INFO,
        .v = {
            .java_class_name = this_class_name_i,
        }};
    class_file->this_class = cf_constant_array_push(&class_file->constant_pool,
                                                    &this_class_info, arena);
  }

  { // Super class
    const u16 constant_java_lang_object_string_i = cf_add_constant_cstring(
        &class_file->constant_pool, "java/lang/Object", arena);

    const cf_constant_t super_class_info = {
        .kind = CONSTANT_POOL_KIND_CLASS_INFO,
        .v = {
            .java_class_name = constant_java_lang_object_string_i,
        }};

    class_file->super_class = cf_constant_array_push(&class_file->constant_pool,
                                                     &super_class_info, arena);
  }
}

static u16 cg_add_method(cf_class_file_t *class_file, u16 access_flags,
                         u16 name, u16 descriptor, cf_attribute_t *attributes,
                         arena_t *arena) {
  pg_assert(class_file != NULL);
  pg_assert(class_file->methods != NULL);

  cf_method_t method = {
      .access_flags = access_flags,
      .attributes = attributes,
      .descriptor = descriptor,
      .name = name,
  };
  pg_array_append(class_file->methods, method, arena);
  return pg_array_last_index(class_file->methods);
}

static void cg_supplement_entrypoint_if_exists(cg_generator_t *gen,
                                               cf_class_file_t *class_file,
                                               arena_t *arena) {
  pg_assert(gen != NULL);
  pg_assert(gen->resolver->parser != NULL);
  pg_assert(class_file != NULL);
  pg_assert(class_file->methods != NULL);

  i32 method_i = -1;
  str source_descriptor_str = str_from_c("([Ljava/lang/String;)V");

  for (u16 i = 0; i < pg_array_len(class_file->methods); i++) {
    const cf_method_t *const method = &class_file->methods[i];

    if ((method->access_flags & (ACCESS_FLAGS_PUBLIC | ACCESS_FLAGS_STATIC)) ==
        0)
      continue;

    // A function not named `main`, skip.
    str name = cf_constant_array_get_as_string(&class_file->constant_pool,
                                               method->name);
    if (!str_eq_c(name, "main"))
      continue;

    str descriptor = cf_constant_array_get_as_string(&class_file->constant_pool,
                                                     method->descriptor);

    // The entrypoint already exists as the JVM expects it, nothing to do.
    if (str_eq(descriptor, source_descriptor_str))
      return;

    // A function named `main` but with a different signature e.g. `fun
    // main(x: Int){}`, skip.
    if (!str_eq_c(descriptor, "()V"))
      continue;

    // At this point, the function is `fun main(){}` and we need to add an
    // JVM entrypoint i.e. `fun main(args: Array<String){ main() }`. Record
    // its index but keep going, in case a later function is already a
    // suitable JVM entrypoint, to avoid duplication.

    pg_assert(method_i == -1);
    method_i = i;
  }
  if (method_i == -1)
    return; // Nothing to do.

  pg_assert((u16)method_i < pg_array_len(class_file->methods));
  const cf_method_t *const target_method = &class_file->methods[method_i];

  cf_attribute_t *attributes = NULL;
  pg_array_init_reserve(attributes, 1, arena);

  // Generate code section for the new method.
  {

    str target_descriptor_string = cf_constant_array_get_as_string(
        &class_file->constant_pool, target_method->descriptor);
    const cf_constant_t target_descriptor = {
        .kind = CONSTANT_POOL_KIND_UTF8,
        .v = {.s = target_descriptor_string},
    };
    const u16 target_descriptor_i = cf_constant_array_push(
        &class_file->constant_pool, &target_descriptor, arena);

    const cf_constant_t target_name_and_type = {
        .kind = CONSTANT_POOL_KIND_NAME_AND_TYPE,
        .v = {.name_and_type = {.name = target_method->name,
                                .descriptor = target_descriptor_i}},
    };
    const u16 target_name_and_type_i = cf_constant_array_push(
        &class_file->constant_pool, &target_name_and_type, arena);
    const cf_constant_t target_method_ref = {
        .kind = CONSTANT_POOL_KIND_METHOD_REF,
        .v = {.method_ref = {.class = class_file->this_class,
                             .name_and_type = target_name_and_type_i}}};
    const u16 target_method_ref_i = cf_constant_array_push(
        &class_file->constant_pool, &target_method_ref, arena);

    const ty_type_method_t target_method_type = {.return_type_i = TYPE_UNIT_I};

    cf_attribute_code_t code = {0};
    pg_array_init_reserve(code.code, 4, arena);

    gen->code = &code;
    gen->frame =
        arena_alloc(arena, sizeof(cg_frame_t), _Alignof(cg_frame_t), 1);
    cg_frame_init(gen->frame, arena);

    // Fill locals (method arguments).
    {
      const ty_type_t string_type = {
          .kind = TYPE_INSTANCE,
          .this_class_name = str_from_c("java/lang/String"),
      };
      pg_array_append(gen->resolver->types, string_type, arena);
      const u32 string_type_i = pg_array_last_index(gen->resolver->types);

      const ty_type_t source_method_argument_types = {
          .kind = TYPE_ARRAY,
          .this_class_name = str_from_c("FIXME"),
          .v = {.array_type_i = string_type_i},
      };
      pg_array_append(gen->resolver->types, source_method_argument_types,
                      arena);

      const u32 source_argument_types_i =
          pg_array_last_index(gen->resolver->types);
      const u16 source_method_arg0_string = cf_add_constant_cstring(
          &class_file->constant_pool, "[Ljava/lang/String;", arena);

      const cf_constant_t source_method_arg0_class = {
          .kind = CONSTANT_POOL_KIND_CLASS_INFO,
          .v = {
              .java_class_name = source_method_arg0_string,
          }};
      const u16 source_method_arg0_class_i = cf_constant_array_push(
          &class_file->constant_pool, &source_method_arg0_class, arena);

      const cf_variable_t arg0 = {
          .node_i = 0,
          .type_i = source_argument_types_i,
          .scope_depth = gen->frame->scope_depth,
          .verification_info =
              {
                  .kind = VERIFICATION_INFO_OBJECT,
                  .extra_data = source_method_arg0_class_i,
              },
      };
      u16 logical_local_index = 0;
      u16 physical_local_index = 0;
      cg_frame_locals_push(gen, &arg0, &logical_local_index,
                           &physical_local_index, arena);
    }

    cg_emit_invoke_static(gen, target_method_ref_i, &target_method_type, arena);
    cg_emit_return_nothing(gen, arena);

    gen->code->max_physical_stack = gen->frame->max_physical_stack;
    gen->code->max_physical_locals = gen->frame->max_physical_locals;
    gen->code = NULL;
    gen->frame = NULL;

    cf_attribute_t attribute_code = {
        .kind = ATTRIBUTE_KIND_CODE,
        .name =
            cf_add_constant_cstring(&class_file->constant_pool, "Code", arena),
        .v = {.code = code}};
    pg_array_append(attributes, attribute_code, arena);
  }

  // Add new method.
  {
    const cf_constant_t source_descriptor = {
        .kind = CONSTANT_POOL_KIND_UTF8,
        .v = {.s = source_descriptor_str},
    };
    const u16 source_descriptor_i = cf_constant_array_push(
        &class_file->constant_pool, &source_descriptor, arena);
    cg_add_method(class_file, ACCESS_FLAGS_PUBLIC | ACCESS_FLAGS_STATIC,
                  target_method->name, source_descriptor_i, attributes, arena);
  }
}

static void cg_emit(resolver_t *resolver, cf_class_file_t *class_file,
                    u32 root_i, arena_t *arena) {
  pg_assert(resolver != NULL);
  pg_assert(class_file != NULL);
  pg_assert(root_i > 0);
  pg_assert(arena != NULL);

  cg_generator_t gen = {.resolver = resolver};
  pg_array_init_reserve(gen.stack_map_frames, 64, arena);
  pg_array_init_reserve(gen.locals, 1 << 12, arena);

  cg_emit_synthetic_class(&gen, class_file, arena);

  if (pg_array_len(resolver->parser->nodes) == 1)
    return;

  cg_emit_node(&gen, class_file, root_i, arena);

  cg_supplement_entrypoint_if_exists(&gen, class_file, arena);
}
