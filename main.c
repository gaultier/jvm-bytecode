#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define u64 uint64_t
#define i64 int64_t
#define u32 uint32_t
#define i32 int32_t
#define u16 uint16_t
#define i16 int16_t
#define u8 uint8_t
#define i8 int8_t

typedef enum {
  CAF_ACC_SUPER = 0x0020,
} class_file_access_flags_t;

typedef struct {
  u16 len;
  u8 *s;
} string_t;

typedef struct {
  enum cp_info_kind_t {
    CIK_UTF8 = 1,
    CIK_INT = 3,
    CIK_FLOAT = 4,
    CIK_LONG = 5,
    CIK_DOUBLE = 6,
    CIK_CLASS_INFO = 7,
    CIK_STRING = 8,
    CIK_FIELD_REF = 9,
    CIK_METHOD_REF = 10,
    CIK_INSTANCE_METHOD_REF = 11,
    CIK_NAME_AND_TYPE = 12,
    CIK_INVOKE_DYNAMIC = 18,
    // CIK_LONG_HIGH,
    // CIK_LONG_LOW,
    // CIK_DOUBLE_HIGH,
    // CIK_DOUBLE_LOW,
  } kind;
  union {
    string_t s;
  } v;
} cp_info_t;

typedef struct {
} field_info_t;

typedef struct {
} method_info_t;

typedef struct {
} attribute_info_t;

const u32 CLASS_FILE_MAGIC_NUMBER = 0xbebafeca;
const u16 CLASS_FILE_MAJOR_VERSION_6 = 50;
const u16 CLASS_FILE_MINOR_VERSION = 0;

typedef struct {
  u32 magic;
  u16 minor_version;
  u16 major_version;
  u16 constant_pool_count;
  cp_info_t *constant_pool;
  u16 access_flags;
  u16 this_class;
  u16 super_class;
  u16 interfaces_count;
  u16 fields_count;
  field_info_t *fields;
  u16 methods_count;
  method_info_t *methods;
  u16 attribute_count;
  attribute_info_t attributes;
} class_file_t;

void file_write_be_16(FILE *file, u16 x) {
  const u16 x_be = htons(x);
  fwrite(&x_be, sizeof(x_be), 1, file);
}

void file_write_be_32(FILE *file, u32 x) {
  const u32 x_be = htonl(x);
  fwrite(&x_be, sizeof(x_be), 1, file);
}

void class_file_write_constant(const class_file_t *class_file, FILE *file,
                               const cp_info_t *constant) {
  switch (constant->kind) {
  case CIK_UTF8: {
    fwrite(&constant->kind, sizeof(constant->kind), 1, file);
    const string_t *const s = (string_t *)&constant->v;
    file_write_be_16(file, s->len);
    fwrite(s->s, sizeof(u8), s->len, file);
    break;
  }
  case CIK_INT:
    assert(0 && "unimplemented");
    break;
  case CIK_FLOAT:
    assert(0 && "unimplemented");
    break;
  case CIK_LONG:
    assert(0 && "unimplemented");
    break;
  case CIK_DOUBLE:
    assert(0 && "unimplemented");
    break;
  case CIK_CLASS_INFO:
    assert(0 && "unimplemented");
    break;
  case CIK_STRING:
    assert(0 && "unimplemented");
    break;
  case CIK_FIELD_REF:
    assert(0 && "unimplemented");
    break;
  case CIK_METHOD_REF:
    assert(0 && "unimplemented");
    break;
  case CIK_INSTANCE_METHOD_REF:
    assert(0 && "unimplemented");
    break;
  case CIK_NAME_AND_TYPE:
    assert(0 && "unimplemented");
    break;
  case CIK_INVOKE_DYNAMIC:
    assert(0 && "unimplemented");
    break;
  default:
    __builtin_unreachable();
  }
}

void class_file_write_constant_pool(const class_file_t *class_file,
                                    FILE *file) {
  const u16 len = class_file->constant_pool_count + 1;
  file_write_be_16(file, len);

  for (u64 i = 0; i < class_file->constant_pool_count; i++) {
    const cp_info_t *const constant = &class_file->constant_pool[i];
    class_file_write_constant(class_file, file, constant);
  }
}
void class_file_write_interfaces(const class_file_t *class_file, FILE *file) {
  assert(0 && "unimplemented");
}

void class_file_write_fields(const class_file_t *class_file, FILE *file) {
  assert(0 && "unimplemented");
}

void class_file_write_methods(const class_file_t *class_file, FILE *file) {
  assert(0 && "unimplemented");
}

void class_file_write(const class_file_t *class_file, FILE *file) {
  fwrite(&class_file->magic, sizeof(class_file->magic), 1, file);

  file_write_be_16(file, class_file->minor_version);
  file_write_be_16(file, class_file->major_version);
  class_file_write_constant_pool(class_file, file);
  file_write_be_16(file, class_file->access_flags);
  file_write_be_16(file, class_file->this_class);
  file_write_be_16(file, class_file->super_class);

  class_file_write_interfaces(class_file, file);
  class_file_write_fields(class_file, file);
  class_file_write_methods(class_file, file);
}

int main() {
  class_file_t class_file = {
      .magic = CLASS_FILE_MAGIC_NUMBER,
      .minor_version = CLASS_FILE_MINOR_VERSION,
      .major_version = CLASS_FILE_MAJOR_VERSION_6,
      .access_flags = CAF_ACC_SUPER,
  };

  class_file_write(&class_file, stdout);
}
