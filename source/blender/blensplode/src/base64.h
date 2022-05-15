#ifndef SRC_BASE64_H
#define SRC_BASE64_H

// FIXME: linked in from ghost
namespace tinygltf {
extern std::string base64_encode(unsigned char const *, unsigned int len);
extern std::string base64_decode(std::string const &s);
}  // namespace tinygltf

#endif /* SRC_BASE64_H */
