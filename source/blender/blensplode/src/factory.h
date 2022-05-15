#ifndef SRC_FACTORY_H
#define SRC_FACTORY_H

#include "BKE_report.h"
#include "BLO_writefile.h"

#include "callable.h"

template<typename new_func_t, typename free_func_t> struct blender_object_factory {
  using object_type = std::remove_pointer_t<return_type<std::remove_pointer_t<new_func_t>>>;
  using return_type = std::shared_ptr<object_type>;
  const new_func_t new_func;
  const free_func_t free_func;

  blender_object_factory(const new_func_t new_func, const free_func_t free_func)
      : new_func(new_func), free_func(free_func)
  {
  }

  template<typename... args_type> return_type operator()(args_type &&...args)
  {
    auto main = new_func(std::forward<args_type>(args)...);
    return_type smain(main, free_func);

    return smain;
  }
};

template<typename new_func_t, typename free_func_t>
blender_object_factory<new_func_t, free_func_t> make_blender_object_factory(
    const new_func_t new_func, const free_func_t free_func)
{
  return blender_object_factory<new_func_t, free_func_t>(new_func, free_func);
}

static auto BKE_main_make = make_blender_object_factory(BKE_main_new, BKE_main_free);
static auto BLO_writer_make = make_blender_object_factory(BLO_writer_new, BLO_writer_free);
static auto BLO_data_reader_make = make_blender_object_factory(BLO_data_reader_new,
                                                               BLO_data_reader_free);
static auto BLO_filedata_make = make_blender_object_factory(BLO_filedata_new, BLO_filedata_free);

inline std::shared_ptr<ReportList> create_report_list(ReportListFlags flags = RPT_PRINT)
{
  auto rl = std::shared_ptr<ReportList>(new ReportList, [](ReportList *ptr) {
    BKE_reports_clear(ptr);

    delete ptr;
  });

  BKE_reports_init(rl.get(), flags);

  return rl;
}

template<typename T, typename Y>
std::shared_ptr<T> blender_wrap_object(T *ptr, Y free)
{
  return std::shared_ptr<T>(ptr, free);
}

template<class T> void blender_generic_free(T ptr)
{
  MEM_freeN(const_cast<void *>(static_cast<const void *>(ptr)));
}

#endif /* SRC_FACTORY_H */
