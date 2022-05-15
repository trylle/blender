#ifndef SRC_CALLABLE_H
#define SRC_CALLABLE_H

// adapted from https://stackoverflow.com/a/54828930
template<class T> struct callable_trait {
};

template<class R, class... Args> struct callable_trait<std::function<R(Args...)>> {
  using return_type = R;
  using argument_types = std::tuple<Args...>;
  static constexpr size_t argument_count = sizeof...(Args);
};

template<typename callable>
using return_type = typename callable_trait<typename std::function<callable>>::return_type;

#endif /* SRC_CALLABLE_H */
