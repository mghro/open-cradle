#include <cradle/inner/dll/dll_controller.h>
#include <cradle/inner/requests/function.h>
#include <cradle/inner/resolve/meta_catalog.h>
#include <cradle/inner/utilities/logging.h>

namespace cradle {

dll_controller::dll_controller(std::string path, std::string name)
    : path_{std::move(path)},
      name_{std::move(name)},
      logger_{ensure_logger("dll")}
{
}

void
dll_controller::load()
{
    logger_->info("load {} (id {}) from {}", name_, cat_id_.value(), path_);
    // TODO Consider rtld_lazy if the library is opened only for getting the
    // uuid's, as this might be significantly faster than rtld_now.
    auto mode{
        boost::dll::load_mode::rtld_now | boost::dll::load_mode::rtld_local
        | boost::dll::load_mode::rtld_deepbind};
    lib_ = std::make_unique<boost::dll::shared_library>(path_, mode);

    typedef void init_func_t();
    std::string const init_func_name{"CRADLE_init"};
    typedef seri_catalog* get_catalog_func_t();
    std::string const get_catalog_func_name{"CRADLE_get_catalog"};

    auto init_func = lib_->get<init_func_t>(init_func_name);
    init_func();

    // TODO if init_func fails, data specific for this DLL may have been stored
    // in the singletons, and would/should be accessible through catalog_ but
    // we don't have retrieved that yet

    auto get_catalog_func
        = lib_->get<get_catalog_func_t>(get_catalog_func_name);
    catalog_ = get_catalog_func();
    cat_id_ = catalog_->get_cat_id();
    meta_catalog::instance().add_catalog(*catalog_);
    logger_->info("load done for {}", name_);
}

void
dll_controller::unload()
{
    logger_->info("unload {} (id {})", name_, cat_id_.value());
    if (cat_id_.is_valid())
    {
        cereal_functions_registry::instance().unregister_catalog(cat_id_);
    }
    meta_catalog::instance().remove_catalog(*catalog_);
    catalog_ = nullptr;
    lib_.reset();
    logger_->info("unload done for {}", name_);
}

} // namespace cradle
