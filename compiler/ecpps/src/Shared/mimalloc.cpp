#if !defined(CLANG_TIDY) && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)
#include <mimalloc-new-delete.h> // NOLINT
#endif
