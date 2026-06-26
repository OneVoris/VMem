#include <voris/mem/buffer_chain.hpp>
#include <voris/mem/page_source.hpp>
#include <voris/mem/resource_ref.hpp>
#include <voris/mem/system_resource.hpp>
#include <voris/mem/unique_buffer.hpp>

#include <cassert>
#include <cstddef>
#include <cstring>

int main()
{
    voris::mem::system_resource system;
    voris::mem::resource_ref resource{system};

    auto buffer = voris::mem::make_unique_buffer(resource, 128U, alignof(std::max_align_t));
    assert(buffer);
    assert(buffer->resize(5U));
    std::memcpy(buffer->data(), "vmem", 5U);

    voris::mem::buffer_chain chain;
    assert(chain.append(std::move(*buffer)));
    assert(chain.size() == 5U);

    voris::mem::os_page_source pages;
    auto page_size = pages.page_size();
    assert(page_size);
    auto span = pages.reserve(*page_size);
    if (span)
    {
        assert(pages.commit(*span));
        assert(pages.decommit(*span));
        assert(pages.release(*span));
    }

    return 0;
}
