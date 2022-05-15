#ifndef SRC_COMMON_H
#define SRC_COMMON_H

#include <json.hpp>

extern std::shared_ptr<BlendFileData> read_blender_file(const char *filepath,
                                                        const BlendFileReadParams &params,
                                                        BlendFileReadReport &bf_reports);

extern bool write_blend_file(const char *filepath, Main *bmain, ReportList *reports = nullptr);

extern nlohmann::json to_safe_json_utf8(const std::string &input);
extern std::string from_safe_json_utf8(const nlohmann::json &input);

#endif /* SRC_COMMON_H */
