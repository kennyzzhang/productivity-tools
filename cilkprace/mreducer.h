
template <typename T>
class mreducer : T {
public:
  template <typename... Args>
  mreducer(Args&&... args) : T(std::forward<Args>(args)...) {
    __cilkrts_reducer_register(static_cast<T*>(this), sizeof(T),
        T::identity, T::reduce);
  }

  ~mreducer() {
    __cilkrts_reducer_unregister(static_cast<T*>(this));
  }
};
