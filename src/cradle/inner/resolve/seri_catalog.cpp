#include <cradle/inner/requests/function.h>
#include <cradle/inner/resolve/seri_catalog.h>

namespace cradle {

seri_catalog::~seri_catalog()
{
    cereal_functions_registry::instance().unregister_catalog(cat_id_);
}

std::vector<std::string>
seri_catalog::get_all_uuid_strs() const
{
    std::vector<std::string> res;
    for (auto const& it : map_)
    {
        res.push_back(it.first);
    }
    return res;
}

} // namespace cradle
