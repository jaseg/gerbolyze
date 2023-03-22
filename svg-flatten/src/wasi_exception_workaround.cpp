
#include <typeinfo>
#include <cstdlib>
using namespace std;

void __cxa_allocate_exception(size_t size) {
    (void) size;
    abort();
}

void __cxa_throw(void* thrown_exception, struct std::type_info * tinfo, void (*dest)(void*)) {
    (void) thrown_exception, (void) tinfo, (void) dest;
    abort();
}

