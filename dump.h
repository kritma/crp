#include "sds.h"
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

sds cant_dump(void *v /*unused*/, sds /*unused*/ p) {
  return sdsnew("<unknown>");
}

bool isSimplePtr = false;

sds (*get_fun(char *name))(void *v, sds spit) {
  sds sds_name = sdsnew(name);
  uint32_t count;
  sdsfreesplitres(sdssplitlen(sds_name, sdslen(sds_name), "*", 1, &count),
                  count);
  if (count == 2) {
    isSimplePtr = true;
  }
  sds *possible_names = sdssplitlen(sds_name, sdslen(sds_name), " ", 1, &count);

  for (int i = 0; i < count; i++) {
    sds full_name = sdscat(possible_names[i], "_to_string");
    sds (*fn)(void *v, sds padding) = dlsym(RTLD_DEFAULT, full_name);
    sdsfree(full_name);

    if (fn) {
      isSimplePtr = true;
      return fn;
    }
  }
  return cant_dump;
}

#define FIELD_TO_STRING(type, name)                                            \
  temp = _Generic((v->name),\
    int32_t: sdscatfmt(padd, "%s(%s): %i", #name, #type, v->name),\
    int64_t: sdscatfmt(padd, "%s(%s): %I", #name, #type, v->name),\
    uint32_t: sdscatfmt(padd, "%s(%s): %u", #name, #type, v->name),\
    uint64_t: sdscatfmt(padd, "%s(%s): %U", #name, #type, v->name),\
    char: sdscatfmt(padd, "%s(%s): %%", #name, #type, v->name),\
    float: sdscatprintf(padd, "%s(%s): %f", #name, #type, v->name),\
    double: sdscatprintf(padd, "%s(%s): %lf", #name, #type, v->name),\
    char *: sdscatfmt(padd, "%s(%s): %s", #name, #type, v->name),\
    const char *: sdscatfmt(padd, "%s(%s): %s", #name, #type, v->name),\
    void*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    uint64_t*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    int32_t*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    int64_t*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    float*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    double*: sdscatprintf(padd, "%s(%s): %p", #name, #type, v->name),\
    default: sdscatfmt(padd, "%s(%s): %S", #name, #type, trash = get_fun(#type)(&v->name, sdscat(sdsdup(padding), "  "))));                                                  \
  out = sdscat(out, temp);                                                     \
  sdsfree(temp);                                                               \
  sdsfree(trash);                                                              \
  trash = NULL;                                                                \
  padd = sdscat(sdsnew("\n  "), padding);

#define DECLARE_FIELD(type, name) type name;

#define DECLARE_STRUCT(TYPE_NAME)                                              \
  typedef struct {                                                             \
    TYPE_NAME##_FIELDS(DECLARE_FIELD)                                          \
  }(TYPE_NAME);                                                                \
  sds TYPE_NAME##_to_string(TYPE_NAME *var, sds padding) {                     \
    TYPE_NAME *v;                                                              \
    if (isSimplePtr) {                                                         \
      v = *(TYPE_NAME **)(void **)(var);                                       \
      isSimplePtr = false;                                                     \
    } else {                                                                   \
      v = var;                                                                 \
    }                                                                          \
    sds out = sdsnew("{\n");                                                   \
    sds temp = sdsempty();                                                     \
    sds padd = sdscat(sdsnew("  "), padding);                                  \
    sds trash = sdsempty();                                                    \
    _Pragma("clang diagnostic push");                                          \
    _Pragma("clang diagnostic ignored \"-Wformat\"");                          \
    TYPE_NAME##_FIELDS(FIELD_TO_STRING);                                       \
    _Pragma("clang diagnostic pop");                                           \
    out = sdscatfmt(out, "\n%S}", padding);                                    \
    return out;                                                                \
  }

#define DUMP(var, TYPE_NAME)                                                   \
  {                                                                            \
    sds str = TYPE_NAME##_to_string(&(var), sdsempty());                       \
    printf("%s\n", str);  \
    sdsfree(str);                                                              \
  }
