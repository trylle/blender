#include "common.h"

#include "BLO_writefile.h"

#include "base64.h"

std::shared_ptr<BlendFileData> read_blender_file(const char *filepath,
                                                 const BlendFileReadParams &params,
                                                 BlendFileReadReport &bf_reports)
{
  BlendFileData *bfd = BKE_blendfile_read(filepath, &params, &bf_reports);

  if (!bfd) {
    return {};
  }

  std::shared_ptr<BlendFileData> sbfd(bfd, BLO_blendfiledata_free);

  return sbfd;
}

bool write_blend_file(const char *filepath, Main *bmain, ReportList *reports)
{
  int fileflags = G.fileflags;
  const eBLO_WritePathRemap remap_mode = BLO_WRITE_PATH_REMAP_NONE;
  // auto *thumb = bmain->blen_thumb;
  BlendFileWriteParams params = {
      .remap_mode = remap_mode, .use_save_versions = false, .use_save_as_copy = false,
      //.thumb = thumb,
  };
  bool success = BLO_write_file(bmain, filepath, fileflags, &params, reports);

  // SET_FLAG_FROM_TEST(G.fileflags, fileflags & G_FILE_COMPRESS, G_FILE_COMPRESS);

#if 0
    /* run this function after because the file can't be written before the blend is */
    if (ibuf_thumb) {
      IMB_thumb_delete(filepath, THB_FAIL); /* without this a failed thumb overrides */
      ibuf_thumb = IMB_thumb_create(filepath, THB_LARGE, THB_SOURCE_BLEND, ibuf_thumb);
    }
#endif

  /* Without this there is no feedback the file was saved. */
  // BKE_reportf(reports, RPT_INFO, "Saved \"%s\"", BLI_path_basename(filepath));
  //}

  return success;
}

nlohmann::json to_safe_json_utf8(const std::string &input)
{
  nlohmann::json v = input;

  try {
    v.dump();

    return v;
  }
  catch (nlohmann::detail::type_error) {
    return {{"base64",
             tinygltf::base64_encode(reinterpret_cast<const unsigned char *>(input.data()),
                                     input.size())}};
  }
}

std::string from_safe_json_utf8(const nlohmann::json &input)
{
  std::string base64 = input.value("base64", {});

  if (!base64.empty()) {
    return tinygltf::base64_decode(base64);
  }

  return input;
}
