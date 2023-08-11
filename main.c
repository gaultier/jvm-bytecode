#include "class_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *usage = "./a.out [-h] (-c classpath) source.kt";

static void print_usage_and_exit() {
  puts(usage);
  exit(0);
}

int main(int argc, char *argv[]) {
  int opt = 0;
  char *classpath = NULL;

  while ((opt = getopt(argc, argv, "vc:")) != -1) {
    switch (opt) {
    case 'v':
      log_verbose = true;
      break;
    case 'c':
      classpath = optarg;
      break;
    case 'h':
      print_usage_and_exit();
      break;

    default:
      fprintf(stderr, "Unknown option: %c\n", opt);
      print_usage_and_exit();
    }
  }

  if (optind == argc) {
    fprintf(stderr, "Missing source file.\n");
    print_usage_and_exit();
  }
  if (optind != argc - 1) {
    fprintf(stderr, "Multiple source files not yet supported.\n");
    print_usage_and_exit();
  }
  if (classpath == NULL) {
    classpath = ".";
  }

  arena_t arena = {0};
  arena_init(&arena, 1L << 29);

  arena_t scratch_arena = {0};
  arena_init(&scratch_arena, 1L << 26); // 64 MiB

  const string_t java_home = find_java_home(&arena);
  LOG("java_home=%.*s", java_home.len, java_home.value);

  string_t *class_path_entries = NULL;
  pg_array_init_reserve(class_path_entries, 16, &arena);
  pg_array_append(class_path_entries, string_make_from_c_no_alloc("."), &arena);

  {
    char *class_path_sep = NULL;
    while ((class_path_sep = strchr(classpath, ':')) != NULL) {
      const string_t class_path_entry = string_make_from_c(classpath, &arena);
      pg_array_append(class_path_entries, class_path_entry, &arena);

      classpath = class_path_sep + 1;
    }
    const string_t class_path_entry = string_make_from_c(classpath, &arena);
    pg_array_append(class_path_entries, class_path_entry, &arena);
  }

  {
    // TODO: when parsing multiple files, need to allocate that.
    const string_t source_file_name = {
        .value = argv[optind],
        .len = strlen(argv[optind]),
    };
    if (!string_ends_with_cstring(source_file_name, ".kt")) {
      fprintf(stderr, "Expected an input file ending with .kt\n");
      exit(EINVAL);
    }

    const int fd = open(source_file_name.value, O_RDONLY);
    if (fd == -1) {
      fprintf(stderr, "Failed to open the file %.*s: %s\n",
              source_file_name.len, source_file_name.value, strerror(errno));
      return errno;
    }

    struct stat st = {0};
    if (stat(source_file_name.value, &st) == -1) {
      fprintf(stderr, "Failed to get the file size %.*s: %s\n",
              source_file_name.len, source_file_name.value, strerror(errno));
      return errno;
    }
    if (st.st_size == 0) {
      return 0;
    }
    if (st.st_size > UINT16_MAX) {
      fprintf(stderr,
              "The file %.*s is too big (limit is %u, got: %ld), stopping.\n",
              source_file_name.len, source_file_name.value, UINT16_MAX,
              st.st_size);
      return E2BIG;
    }

    string_t source = {0};
    int res = ut_read_all_from_fd(fd, st.st_size, &source, &arena);
    if (res != -1) {
      fprintf(stderr, "Failed to read the full file %.*s: %s\n",
              source_file_name.len, source_file_name.value, strerror(res));
      return res;
    }
    close(fd);

    // Lex.
    lex_lexer_t lexer = {
        .file_path = source_file_name,
    };
    pg_array_init_reserve(lexer.tokens, source.len, &arena);
    pg_array_init_reserve(lexer.line_table, source.len, &arena);

    const char *current = source.value;
    lex_lex(&lexer, source.value, source.len, &current, &arena);

    // Parse.
    par_parser_t parser = {
        .buf = source.value,
        .buf_len = source.len,
        .lexer = &lexer,
    };
    const u32 root_i = par_parse(&parser, &arena);

    if (parser.state != PARSER_STATE_OK)
      return 1; // TODO: Should type checking still proceed?

    resolver_t resolver = {
        .parser = &parser,
        .class_path_entries = class_path_entries,
    };
    pg_array_init_reserve(resolver.variables, 512, &arena);
    pg_array_init_reserve(resolver.types, 1 << TYPES_EXP, &arena);
    const ty_type_t any_type = {
        .this_class_name = string_make_from_c("kotlin.Any", &arena),
        .descriptor = string_make_from_c("Lkotlin.Any;", &arena),
    };
    resolver_intern_type(&resolver, &any_type);

    const ty_type_t unit_type = {
        .kind = TYPE_JVM_VOID,
        .flag = TYPE_FLAG_KOTLIN_UNIT,
        .this_class_name = string_make_from_c("kotlin.Unit", &arena),
        .descriptor = string_make_from_c("Lkotlin.Unit;", &arena),
    };
    resolver_intern_type(&resolver, &unit_type);

    resolver_load_standard_types(&resolver, java_home, &scratch_arena, &arena);
    arena_clear(&scratch_arena);

    resolver_resolve_node(&resolver, root_i, &scratch_arena, &arena);
    arena_clear(&scratch_arena);

    // Debug types.
    {
      LOG("------ After type checking%s", "");
      arena_t tmp_arena = arena;
      resolver_ast_fprint_node(&resolver, root_i, stderr, 0, &tmp_arena);
    }

    if (parser.state != PARSER_STATE_OK)
      return 1;

    // Emit bytecode.
    cf_class_file_t class_file = {
        .class_file_path = cf_make_class_file_path_kt(source_file_name, &arena),
        .minor_version = 0,
        .major_version =
            17, // TODO: Add a CLI option to choose the jdk/jre version
        .access_flags = ACCESS_FLAGS_SUPER | ACCESS_FLAGS_PUBLIC,
    };
    cf_init(&class_file, &arena);
    cg_emit(&resolver, &class_file, root_i, &scratch_arena, &arena);
    if (parser.state != PARSER_STATE_OK)
      return 1;

    FILE *file = fopen(class_file.class_file_path.value, "w");
    if (file == NULL) {
      fprintf(stderr, "Failed to open the file %.*s: %s\n",
              source_file_name.len, source_file_name.value, strerror(errno));
      return errno;
    }
    cf_write(&class_file, file);
    fclose(file);

    LOG("nodes=%lu sizeof(ty_type_t)=%lu mem=%lu", pg_array_len(parser.nodes),
        sizeof(par_ast_node_t),
        pg_array_len(parser.nodes) * sizeof(par_ast_node_t));
    LOG("types=%lu sizeof(ty_type_t)=%lu mem=%lu", pg_array_len(resolver.types),
        sizeof(ty_type_t), pg_array_len(resolver.types) * sizeof(ty_type_t));
    {
      arena_t tmp_arena = arena;
      LOG("\n----------- Verifiying%s", "");

      int fd = open(class_file.class_file_path.value, O_RDONLY);
      if (fd == -1) {
        fprintf(stderr, "Failed to open the file %.*s: %s\n",
                source_file_name.len, source_file_name.value, strerror(errno));
        return errno;
      }

      struct stat st = {0};
      if (stat(class_file.class_file_path.value, &st) == -1) {
        fprintf(stderr, "Failed to get the file size %.*s: %s\n",
                source_file_name.len, source_file_name.value, strerror(errno));
        return errno;
      }
      pg_assert(st.st_size > 0);
      pg_assert(st.st_size <= UINT32_MAX);

      const u32 buf_len = st.st_size;
      char *const buf = arena_alloc(&tmp_arena, buf_len, sizeof(u8));

      pg_assert(read(fd, buf, buf_len) == buf_len);
      close(fd);

      cf_class_file_t class_file_verify = {.class_file_path =
                                               class_file.class_file_path};
      char *current = buf;
      cf_buf_read_class_file(buf, buf_len, &current, &class_file_verify,
                             &tmp_arena);
    }
  }
  LOG("arena=%lu", arena.current_offset);
}
