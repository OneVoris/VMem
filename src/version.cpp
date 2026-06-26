#include <voris/mem/version.hpp>

namespace voris::mem
{

std::string_view version() noexcept
{
    return library_version;
}

} // namespace voris::mem
