#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <json.hpp>
#include <type_traits>

#include "cxxopts.hpp"

#include "BKE_appdir.h"
#include "BKE_blender.h"
#include "BKE_blender_version.h"
#include "BKE_blendfile.h"
#include "BKE_callbacks.h"
#include "BKE_global.h"
#include "BKE_idtype.h"
#include "BKE_main.h"
#include "BKE_modifier.h"
#include "BKE_node.h"
#include "BKE_object.h"
#include "BKE_report.h"
#include "BKE_vfont.h"
#include "BLI_endian_defines.h"
#include "BLI_listbase.h"
#include "BLI_threads.h"
#include "BLO_read_write.h"
#include "BLO_readfile.h"
#include "BLO_writefile.h"
#include "CLG_log.h"
#include "DEG_depsgraph.h"
#include "DNA_genfile.h"
#include "DNA_windowmanager_types.h"
#include "ED_datafiles.h"
#include "GHOST_Path-api.h"
#include "IMB_imbuf.h"
#include "MEM_guardedalloc.h"
#include "RNA_define.h"

#include "readfile.h"

#include "explode.h"
#include "factory.h"
#include "implode.h"

int main(int argc, char *argv[])
{
  using namespace std;

  cxxopts::Options options(argc > 0 ? argv[0] : "blensplode", "Blender format exploder/imploder");

  options.add_options()                   //
      ("h,help", "produce help message")  //
      ("i,implode",
       "implode file instead of explode",
       cxxopts::value<bool>()->default_value("false"))  //
      ("input-file", "input file", cxxopts::value<std::string>());

  options.parse_positional({"input-file"});
  options.positional_help("<input-file>");

  auto result = options.parse(argc, argv);

  if (result.count("help") || result.count("input-file") == 0) {
    cout << options.help() << "\n";
    return 1;
  }

  // VS Code debugger struggles with many threads
  BLI_system_num_threads_override_set(1);

  // Copied from BlendfileLoadingBaseTest::SetUpTestCase
  CLG_init();
  BLI_threadapi_init();

  DNA_sdna_current_init();
  BKE_blender_globals_init();

  BKE_idtype_init();
  BKE_appdir_init();
  IMB_init();
  BKE_modifier_init();
  DEG_register_node_types();
  RNA_init();
  BKE_node_system_init();
  BKE_callback_global_init();
  BKE_vfont_builtin_register(datatoc_bfont_pfb, datatoc_bfont_pfb_size);

  G.background = true;
  G.factory_startup = true;

  /* Allocate a dummy window manager. The real window manager will try and load Python scripts from
   * the release directory, which it won't be able to find. */
  // ASSERT_EQ(G.main->wm.first, nullptr);
  G.main->wm.first = MEM_callocN(sizeof(wmWindowManager), __func__);

  auto input_file = result["input-file"].as<string>();
  bool success = false;

  if (result.count("implode") == 0) {
    success = explode_blend_file(input_file.c_str());
  }
  else {
    success = implode_blend_file(input_file.c_str());
  }

  // Copied from BlendfileLoadingBaseTest::TearDownTestCase
  if (G.main->wm.first != nullptr) {
    MEM_freeN(G.main->wm.first);
    G.main->wm.first = nullptr;
  }

  /* Copied from WM_exit_ex() in wm_init_exit.c, and cherry-picked those lines that match the
   * allocation/initialization done in SetUpTestCase(). */
  BKE_blender_free();
  RNA_exit();

  DEG_free_node_types();
  GHOST_DisposeSystemPaths();
  DNA_sdna_current_free();
  BLI_threadapi_exit();

  BKE_blender_atexit();

  BKE_tempdir_session_purge();
  BKE_appdir_exit();
  CLG_exit();

  if (!success) {
    return -1;
  }

  std::cout << "Done" << std::endl;

  return 0;
}
