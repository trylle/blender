#ifndef SRC_ENDIAN_H
#define SRC_ENDIAN_H

#include "BLI_endian_switch.h"

struct endian_switch_t {
  const bool do_switch;

  endian_switch_t(bool do_switch) : do_switch(do_switch)
  {
  }

  template<class T> void operator()(T &obj)
  {
    if (!do_switch)
      return;

    perform_switch<T>(obj);
  }

  template<class T> void perform_switch(T &obj);

  template<> void perform_switch(std::int32_t &obj)
  {
    BLI_endian_switch_int32(&obj);
  }

  template<> void perform_switch(std::int16_t &obj)
  {
    BLI_endian_switch_int16(&obj);
  }

  template<> void perform_switch(std::uint16_t &obj)
  {
    BLI_endian_switch_uint16(&obj);
  }

  template<> void perform_switch(std::uint32_t &obj)
  {
    BLI_endian_switch_uint32(&obj);
  }

  template<> void perform_switch(std::float_t &obj)
  {
    BLI_endian_switch_float(&obj);
  }

  template<> void perform_switch(std::int64_t &obj)
  {
    BLI_endian_switch_int64(&obj);
  }

  template<> void perform_switch(std::uint64_t &obj)
  {
    BLI_endian_switch_uint64(&obj);
  }

  template<> void perform_switch(std::double_t &obj)
  {
    BLI_endian_switch_double(&obj);
  }
};

#endif /* SRC_ENDIAN_H */
