#include "implode.h"

#include <filesystem>
#include <fstream>

#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/lexical_cast.hpp>

#include <json.hpp>

#include "BLI_endian_defines.h"
#include "BLI_endian_switch.h"
#include "BLO_blend_defs.h"
#include "DNA_sdna_types.h"

#include "base64.h"
#include "common.h"
#include "endian.h"

bool implode_blend_file(const char *filepath)
{
  std::ifstream i(filepath);
  nlohmann::ordered_json j;

  i >> j;

  const auto &h = j["header"];
  short version = h["version"];
  int endian_order = h["endianness"] == "big" ? B_ENDIAN : L_ENDIAN;
  std::size_t pointer_size = h["pointer_size"];
  bool endian_switch_required = ENDIAN_ORDER != endian_order;

  std::filesystem::path filepath_out(filepath);

  filepath_out.replace_extension(".blend.tmp");

  std::ofstream o(filepath_out, std::ios::binary);
  std::string header(12 + 1, 0);

  snprintf(header.data(),
           header.size(),
           "BLENDER%c%c%.3d",
           (pointer_size == 8) ? '-' : '_',
           (endian_order == B_ENDIAN) ? 'V' : 'v',
           version);

  o.write(header.data(), header.size() - 1);

  const auto &d = j["datablocks"];

  for (const auto &datablock_entry : d) {
    auto code = from_safe_json_utf8(datablock_entry.value<nlohmann::json>("code", {}));
    auto old_str = datablock_entry.value<std::string>("old", "0");
    auto old = std::stoull(old_str, nullptr, 0);
    auto SDNAnr = datablock_entry.value<int>("SDNAnr", 0);
    auto nr = datablock_entry.value<int>("nr", 1);
    auto file = datablock_entry.value<std::string>("file", {});
    auto data = datablock_entry.value<std::string>("data", {});
    std::vector<char> datablock;

    if (!file.empty()) {
      std::filesystem::path p(filepath);

      p = p.remove_filename() / "datablocks" / file;

      boost::iostreams::mapped_file src_mmap(p, boost::iostreams::mapped_file::readonly);

      datablock.assign(src_mmap.const_begin(), src_mmap.const_end());
    }
    else {
      auto s = tinygltf::base64_decode(data);

      datablock.assign(s.begin(), s.end());
    }

    BHead8 bh8 = {};

    std::copy(code.begin(), code.end(), reinterpret_cast<std::uint8_t *>(&bh8.code));
    bh8.len = datablock.size();
    bh8.nr = nr;
    bh8.SDNAnr = SDNAnr;
    bh8.old = old;

    if (endian_switch_required) {
      endian_switch_t esw(true);

      if (bh8.code != ENDB) {
        esw(bh8.len);
        esw(bh8.SDNAnr);
        esw(bh8.nr);
      }

      if ((bh8.code & 0xFFFF) == 0) {
        bh8.code >>= 16;
      }
    }

    if (pointer_size == 8) {
      o.write(reinterpret_cast<const char *>(&bh8), sizeof(bh8));
    } else {
      BHead4 bh4 = {
          .code = bh8.code,
          .len = bh8.len,
          .old = static_cast<std::uint32_t>(bh8.old),
          .SDNAnr = bh8.SDNAnr,
          .nr = bh8.nr,
      };

      o.write(reinterpret_cast<const char *>(&bh4), sizeof(bh4));
    }

    o.write(datablock.data(), datablock.size());

    if (o.bad()) {
      std::cerr << "Error writing " << filepath_out << std::endl;

      return false;
    }
  }

  return true;
}
