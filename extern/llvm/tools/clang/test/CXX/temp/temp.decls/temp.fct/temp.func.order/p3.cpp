// RUN: %clang_cc1 -fsyntax-only -verify %s

namespace DeduceVsMember {
  template<typename T>
  struct X {
    template<typename U>
    int &operator==(const U& other) const;
  };

  template<typename T, typename U>
  float &operator==(const T&, const X<U>&);

  void test(X<int> xi, X<float> xf) {
    float& ir = (xi == xf);
  }
}
