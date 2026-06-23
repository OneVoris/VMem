#include <voris/mem/version.hpp>

#include <cassert>

int main() {
    assert(!voris::mem::version().empty());
    return 0;
}
