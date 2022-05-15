#include "explode.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>

#include <zstd.h>
#include <zstd_errors.h>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/iostreams/device/mapped_file.hpp>

#include <json.hpp>

// #define TINYGLTF_NO_STB_IMAGE
// #define TINYGLTF_NO_STB_IMAGE_WRITE
// #define TINYGLTF_IMPLEMENTATION

// #include <tiny_gltf.h>

#include "BKE_blender_version.h"
#include "BLI_endian_defines.h"
#include "BLI_endian_switch.h"
#include "BLI_hash_md5.h"
#include "BLO_blend_defs.h"
#include "BLO_writefile.h"
#include "DNA_genfile.h"
#include "DNA_sdna_types.h"
#include "DNA_text_types.h"
#include "MEM_guardedalloc.h"

#include "common.h"
#include "endian.h"
#include "factory.h"
#include "base64.h"

std::string safe_filename(const std::string &id_name)
{
  char hexdigest[33] = {0};
  unsigned char digest[16] = {0};

  BLI_hash_md5_buffer(id_name.c_str(), id_name.size(), digest);
  BLI_hash_md5_to_hexdigest(digest, hexdigest);

  auto filtered = std::regex_replace(id_name, std::regex(R"([^a-zA-Z0-9\-_])"), "");

  hexdigest[8] = 0;  // 8 characters of hash is enough

  boost::algorithm::to_lower(filtered);

  filtered += std::string("_") + hexdigest;

  return filtered;
}

std::string filenameize_id(const ID *id)
{
  std::string ext = "raw";
  std::string id_name = id->name + 2;
  char hexdigest[33] = {0};
  unsigned char digest[16] = {0};

  BLI_hash_md5_buffer(id_name.c_str(), id_name.size(), digest);
  BLI_hash_md5_to_hexdigest(digest, hexdigest);

  auto filtered = std::regex_replace(id_name, std::regex(R"([^a-zA-Z0-9])"), "");

  hexdigest[8] = 0;  // 8 characters of hash is enough

  boost::algorithm::to_lower(filtered);

  filtered += std::string("_") + hexdigest;

  if (!ext.empty()) {
    filtered += "." + ext;
  }

  return filtered;
}

std::optional<std::tuple<std::size_t, int, short>> parse_header(const char *&begin,
                                                                const char *end)
{
  if (end - begin < 12) {
    return std::nullopt;
  }

  std::string expected_name = "BLENDER";

  if (!std::equal(expected_name.begin(), expected_name.end(), begin)) {
    return std::nullopt;
  }

  std::size_t ptr_size = begin[7] == '-' ? 8 : 4;
  int endian_order = begin[8] == 'V' ? B_ENDIAN : L_ENDIAN;
  short version = 0;

  std::from_chars(begin + 9, begin + 12, version);

  begin += 12;

  return {{ptr_size, endian_order, version}};
}

struct file_data {
  short version;
  std::size_t ptr_size;
  int endian_order;

  bool endian_switch_required() const
  {
    return ENDIAN_ORDER != endian_order;
  }

  endian_switch_t esw() const
  {
    return endian_switch_t(endian_switch_required());
  }
};

namespace detail {
template<class BHead_src>
std::optional<BHead> parse_block_header_to_native(const char *&src_begin,
                                                  const char *src_end,
                                                  const file_data &fd)
{
  auto available = src_end - src_begin;
  BHead native;

  if (available < sizeof(BHead_src)) {
    return std::nullopt;
  }

  const auto *bh8 = reinterpret_cast<const BHead_src *>(src_begin);

  native = BHead{
      .code = bh8->code,
      .len = bh8->len,
      .old = reinterpret_cast<const void *>(bh8->old),
      .SDNAnr = bh8->SDNAnr,
      .nr = bh8->nr,
  };

  src_begin += sizeof(*bh8);

  return native;
}
}  // namespace detail

std::optional<BHead> parse_block_header(const char *&src_begin,
                                        const char *src_end,
                                        const file_data &fd)
{
  std::optional<BHead> result;

  if (fd.ptr_size == 8) {
    result = detail::parse_block_header_to_native<BHead8>(src_begin, src_end, fd);
  }
  else {
    result = detail::parse_block_header_to_native<BHead4>(src_begin, src_end, fd);
  }

  if (!result) {
    return std::nullopt;
  }

  BHead native = *result;

  if (fd.endian_switch_required()) {
    if ((native.code & 0xFFFF) == 0) {
      native.code >>= 16;
    }

    if (native.code != ENDB) {
      fd.esw()(native.len);
      fd.esw()(native.SDNAnr);
      fd.esw()(native.nr);
    }
  }

  return native;
}

bool decompress(std::vector<char> &uncompressed_data, const char *&src_begin, const char *&src_end)
{
  ZSTD_DCtx *const dctx = ZSTD_createDCtx();

  if (!dctx) {
    std::cerr << "ZSTD_createDCtx() failed!" << std::endl;

    return false;
  }

  uncompressed_data.resize((src_end - src_begin) / 2);

  ZSTD_inBuffer input = {src_begin, static_cast<size_t>(src_end - src_begin), 0};
  ZSTD_outBuffer output = {nullptr, 0, 0};

  while (input.pos < input.size) {
    output.dst = uncompressed_data.data();
    output.size = uncompressed_data.size();

    /* The return code is zero if the frame is complete, but there may
     * be multiple frames concatenated together. Zstd will automatically
     * reset the context when a frame is complete. Still, calling
     * ZSTD_DCtx_reset() can be useful to reset the context to a clean
     * state, for instance if the last decompression call returned an
     * error.
     */
    size_t const zstd_result = ZSTD_decompressStream(dctx, &output, &input);

    if (ZSTD_isError(zstd_result)) {
      if (ZSTD_getErrorCode(zstd_result) == ZSTD_error_dstSize_tooSmall) {
        uncompressed_data.resize(uncompressed_data.size() * 2);

        continue;
      }

      std::cerr << ZSTD_getErrorName(zstd_result) << std::endl;

      return false;
    }
  }

  uncompressed_data.resize(output.pos);
  src_begin = uncompressed_data.data();
  src_end = src_begin + uncompressed_data.size();

  return true;
}

std::string create_code_str(int code)
{
  return {reinterpret_cast<const char *>(&code), reinterpret_cast<const char *>(&code + 1)};
}

static void switch_endian_structs(const struct SDNA *filesdna, const BHead *bhead, char *data)
{
  int blocksize, nblocks;

  blocksize = filesdna->types_size[filesdna->structs[bhead->SDNAnr]->type];
  nblocks = bhead->nr;

  while (nblocks--) {
    DNA_struct_switch_endian(filesdna, bhead->SDNAnr, data);

    data += blocksize;
  }
}

std::tuple<std::vector<char>, eSDNA_StructCompare> get_valid_datablock(
    const file_data &fd,
    const SDNA *memsdna,
    const SDNA *filesdna,
    const DNA_ReconstructInfo *reconstruct_info,
    const char *compare_flags,
    const BHead &bh,
    const char *datablock_bytes)
{
  std::vector<char> temp(datablock_bytes, datablock_bytes + bh.len);

  // adapted from read_struct
  if (!bh.len || !bh.SDNAnr) {
    return {temp, SDNA_CMP_UNKNOWN};
  }

  if (fd.endian_switch_required()) {
    switch_endian_structs(filesdna, &bh, temp.data());
  }

  auto compare_flag = eSDNA_StructCompare(compare_flags[bh.SDNAnr]);

  if (compare_flag == SDNA_CMP_REMOVED) {
    return {temp, SDNA_CMP_REMOVED};
  }

  if (compare_flag == SDNA_CMP_NOT_EQUAL) {
    auto reconstructed_blocks = blender_wrap_object(
        static_cast<const char *>(
            DNA_struct_reconstruct(reconstruct_info, bh.SDNAnr, bh.nr, datablock_bytes)),
        blender_generic_free<const char *>);
    const SDNA_Struct *old_struct = filesdna->structs[bh.SDNAnr];
    const char *type_name = filesdna->types[old_struct->type];
    const int new_struct_nr = DNA_struct_find_nr(memsdna, type_name);
    const SDNA_Struct *new_struct = memsdna->structs[new_struct_nr];
    const int new_block_size = memsdna->types_size[new_struct->type];
    auto buffer_size = bh.nr * new_block_size;

    temp.assign(reconstructed_blocks.get(), reconstructed_blocks.get() + buffer_size);
  }

  return {temp, compare_flag};
}

bool explode_blend_file(const char *filepath)
{
  boost::iostreams::mapped_file src_mmap(filepath, boost::iostreams::mapped_file::readonly);
  const auto *src_begin = src_mmap.const_data();
  const auto *src_end = src_begin + src_mmap.size();
  std::vector<char> uncompressed_data;

  auto result = parse_header(src_begin, src_end);

  if (!result) {
    if (decompress(uncompressed_data, src_begin, src_end)) {
      result = parse_header(src_begin, src_end);
    }
  }

  if (!result) {
    std::cerr << "Could not parse blender header" << std::endl;

    return false;
  }

  auto [ptr_size, endian_order, version] = *result;
  file_data fd = {
      .version = version,
      .ptr_size = ptr_size,
      .endian_order = endian_order,
  };

  nlohmann::ordered_json j;

  {
    auto &h = j["header"];

    h["version"] = version;
    h["endianness"] = endian_order == B_ENDIAN ? "big" : "little";
    h["pointer_size"] = ptr_size;
  }

  {
    auto &d = j["datablocks"];
    auto datablocks_path = std::filesystem::path("datablocks");

    std::filesystem::create_directories(datablocks_path);

    {
      std::vector<std::tuple<std::string, BHead, const char *>> datablocks;

      for (;;) {
        auto result = parse_block_header(src_begin, src_end, fd);

        if (!result) {
          break;
        }

        auto bhead = *result;
        std::string code = create_code_str(bhead.code);

        datablocks.emplace_back(code, bhead, src_begin);

        src_begin += bhead.len;
      }

      const auto *memsdna = DNA_sdna_current_get();
      std::shared_ptr<SDNA> filesdna;
      std::shared_ptr<DNA_ReconstructInfo> reconstruct_info;
      std::shared_ptr<const char> compare_flags;

      {
        auto dna_it = std::find_if(datablocks.begin(), datablocks.end(), [](auto v) {
          auto [code, head, bytes] = v;

          return code == create_code_str(DNA1);
        });

        if (dna_it == datablocks.end()) {
          std::cerr << "No DNA1 block found" << std::endl;

          return false;
        }

        auto [_, dnaheader, dnablock] = *dna_it;

        filesdna = blender_wrap_object(
            DNA_sdna_from_data(
                dnablock, dnaheader.len, fd.endian_switch_required(), true, nullptr),
            DNA_sdna_free);

        compare_flags = blender_wrap_object(DNA_struct_get_compareflags(filesdna.get(), memsdna),
                                            blender_generic_free<const char *>);

        reconstruct_info = blender_wrap_object(
            DNA_reconstruct_info_create(filesdna.get(), memsdna, compare_flags.get()),
            DNA_reconstruct_info_free);
      }

      auto lookup_datablock_by_old = [&](const void *old) {
        auto dna_it = std::find_if(datablocks.begin(), datablocks.end(), [&](const auto &v) {
          auto [code, head, bytes] = v;

          return head.old == old;
        });

        return dna_it;
      };

      for (auto [code, bh, datablock_bytes] : datablocks) {
        auto [valid_datablock_bytes, compare_flag] = get_valid_datablock(fd,
                                                                         memsdna,
                                                                         filesdna.get(),
                                                                         reconstruct_info.get(),
                                                                         compare_flags.get(),
                                                                         bh,
                                                                         datablock_bytes);
        std::string id_name;
        std::string type_name;

        d.push_back({
            {"code", to_safe_json_utf8(code)},
        });

        if (bh.old) {
          d.back()["old"] = boost::str(boost::format("%x") % bh.old);
        }

        if (bh.nr != 1) {
          d.back()["nr"] = bh.nr;
        }

        if (bh.SDNAnr != 0) {
          d.back()["SDNAnr"] = bh.SDNAnr;
        }

        if (valid_datablock_bytes.empty()) {
          continue;
        }

        ID *id = nullptr;

        if (bh.SDNAnr) {
          const SDNA_Struct *old_struct = filesdna->structs[bh.SDNAnr];

          type_name = filesdna->types[old_struct->type];

          bool has_id = old_struct->members_len > 0 &&
                        filesdna->types[old_struct->members[0].type] == std::string("ID");

          if (has_id) {
            id = reinterpret_cast<ID *>(valid_datablock_bytes.data());
            id_name = id->name;
            d.back()["name"] = to_safe_json_utf8(id_name);
          }

          d.back()["type"] = to_safe_json_utf8(type_name);
        }

        auto datablock_num_bytes = bh.len;

#if 0  // TODO
        if (type_name == std::string("TextLine")) {
          auto *text_line = reinterpret_cast<TextLine *>(valid_datablock_bytes.data());

          auto dna_it = lookup_datablock_by_old(text_line->line);

          if (dna_it != datablocks.end()) {
            auto [code, head, bytes] = *dna_it;

            d.back()["data"] = {{"text", bytes}};

            continue;
          }
        }
#endif

        if (datablock_num_bytes > 100) {
          std::string fn;

          auto safest_filename = [](std::string fn) {
            fn = safe_filename(fn);

            static std::map<std::string, int> counts;
            auto current_count = ++counts[fn];

            if (current_count > 1) {
              fn += boost::str(boost::format("_%03d") % current_count);
            }

            return fn;
          };

          if (!id_name.empty()) {
            fn = safest_filename(type_name + id_name);
          }
          else {
            std::string safe_type_name = type_name;

            if (safe_type_name.empty()) {
              safe_type_name = code;
            }

            fn = safest_filename(safe_type_name);
          }

          auto path = datablocks_path / fn;

          d.back()["file"] = fn;

          std::ofstream ofs(path, std::ios::binary);

          ofs.write(datablock_bytes, datablock_num_bytes);

          if (ofs.bad()) {
            std::cerr << "Failed to write datablock to file: " << fn << std::endl;

            return false;
          }
        }
        else {
          d.back()["data"] = tinygltf::base64_encode(
              reinterpret_cast<const unsigned char *>(datablock_bytes), datablock_num_bytes);
        }
      }
    }
  }

  std::filesystem::path orig_fp = filepath;

  {
    std::ofstream ofs(std::filesystem::path(orig_fp).replace_extension("json"));

    ofs << std::setw(2) << j;
  }

  return true;
}