#pragma once

// oc: my own common headers

#include "fmt/base.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>
#include <iostream>
#include <fmt/core.h>
#include <cfloat>
#include <bitset>
#include <type_traits>
#include <atomic>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;


typedef size_t usize;
// #define byte u8
typedef u8 byte;

#define F32_MAX FLT_MAX

// never forget to call this!
void oc_init();

template <typename T, usize N>
constexpr usize array_size(T (&)[N]) {
    return N;
}

// a simple panic function that prints a message and then aborts the program
// it should be used for unrecoverable errors (programmer errors).
template <typename ...Args>
__attribute__((noreturn))
void panic(fmt::format_string<Args...> fmt, Args... args);

template <typename T>
void assert(T t);

// the macro `defer` allows for syntax similar to golang or zig.
// it is useful when you want to run some code at the end of a scope, without
// wrapping stuff into objects and using RAII.
template <typename F>
class deferred_call {
public:
    deferred_call(F f) : f(std::move(f)) {}

    ~deferred_call() {
        f();
    }

    F f;
};

#define DEFER_CONCAT_IMPL(x, y) x##y
#define DEFER_CONCAT(x, y) DEFER_CONCAT_IMPL(x, y)
#define DEFER_VAR(x) DEFER_CONCAT(x, __COUNTER__)
#define defer(...) deferred_call DEFER_VAR(_)([&](){__VA_ARGS__;})

// slices are just a plain pointer and a length. The data in them is not owned.
template <typename T>
struct slice {
    T* data;
    usize len;

    inline constexpr slice() : data(nullptr), len(0) {}
    inline constexpr slice(T *data, usize len) : data(data), len(len) {}

    template <typename U>
    bool operator ==(const slice<U>& other) const {
        // @todo: optimize this
        if (len != other.len) {
            return false;
        }
        for (usize i = 0; i < len; i++) {
            if (data[i] != other.data[i]) {
                return false;
            }
        }
        return true;
    }

    inline constexpr slice<T> sub(usize start, usize end) const {
        return { data + start, end - start };
    }

    inline constexpr slice<T> sub(usize start) const {
        return { data + start, len - start };
    }

    inline constexpr T& operator[](usize i) const {
        return data[i];
    }

    inline usize find(T value) const {
        // @todo: this could be heavily optimized for bytes using std::memchr.
        if constexpr (std::is_same_v<T, byte>) {
            auto ptr = std::memchr(data, value, len);
            if (ptr) {
                return (usize)((byte *)ptr - data);
            } else {
                return len;
            }
        } else {
            for (usize i = 0; i < len; i++) {
                if (data[i] == value) {
                    return i;
                }
            }
            return len;
        }
    }

    inline constexpr slice<T> take_until(T value) const {
        usize i = find(value);
        if (i == len) {
            return *this;
        }
        return sub(0, i);
    }

    inline constexpr void split(T value, slice<T> &before, slice<T> &after) const {
        usize i = find(value);
        if (i == len) {
            before = *this;
            after = {};
            return;
        }
        before = sub(0, i);
        after = sub(i + 1);
        return;
    }

    // slices should always be castable to slice<byte>.
    inline constexpr operator slice<byte>() const {
        return { (byte *)data, len * sizeof(T) };
    }

    T *begin() const {
        return data;
    }

    T *end() const {
        return data + len;
    }
};

#define S(x) slice<const char>(x, sizeof(x) - 1)

// formatting of slices
template <typename T>
struct fmt::formatter<slice<T>> {
    bool as_string = false;

    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}') {
            if (*it == 's') {
                as_string = true;
            }
            ++it;
        }
        return it;
    }

    auto format(const slice<T> &s, auto& ctx) const {
        if(as_string) {
            return format_to(ctx.out(), "\"{}\"", std::string((char *)s.data, s.len));
        }

        // format as [Inner, Inner, ...]
        auto it = format_to(ctx.out(), "[");
        for (usize i = 0; i < s.len; i++) {
            if (i > 0) {
                it = format_to(it, ", ");
            }
            it = format_to(it, "{}", s.data[i]);
        }
        return format_to(it, "]");
    }
};

// error type
template <typename T, typename E>
struct result_t {
    bool is_value;
    union {
        T value;
        E error;
    };

    inline constexpr result_t(T value) : is_value(true), value(value) {}
    inline constexpr result_t(E error) : is_value(false), error(error) {}

    inline static constexpr result_t<T, E> ok(T value) {
        return result_t<T, E>(value);
    }

    inline static constexpr result_t<T, E> err(E error) {
        return result_t<T, E>(error);
    }

    inline constexpr result_t<T, E> operator()(T value) {
        return result_t<T, E>(value);
    }

    inline constexpr result_t<T, E> operator()(E error) {
        return result_t<T, E>(error);
    }

    inline constexpr ~result_t() {
        if (is_value) {
            value.~T();
        } else {
            error.~E();
        }
    }

    inline constexpr result_t(const result_t<T, E> &other) : is_value(other.is_value) {
        if (is_value) {
            value = other.value;
        } else {
            error = other.error;
        }
    }

    inline constexpr bool is_ok() const {
        return is_value;
    }

    inline constexpr bool is_err() const {
        return !is_value;
    }

    inline constexpr T unwrap() const {
        if(!is_value) {
            // panic and try to print the error
            panic("unwrap() on result which is error:\n\t{}", error);
        }

        return value;
    }

    inline constexpr E unwrap_err() const {
        return error;
    }
};

template <typename E>
struct result_t<void, E> {
    bool is_value;
    union {
        E error;
    };

    inline constexpr result_t() : is_value(true) {}
    inline constexpr result_t(E error) : is_value(false), error(error) {}

    inline static constexpr result_t<void, E> ok() {
        return result_t<void, E>();
    }

    inline static constexpr result_t<void, E> err(E error) {
        return result_t<void, E>(error);
    }

    inline constexpr result_t<void, E> operator()() {
        return result_t<void, E>();
    }

    inline constexpr result_t<void, E> operator()(E error) {
        return result_t<void, E>(error);
    }

    inline constexpr ~result_t() {
        if (!is_value) {
            error.~E();
        }
    }

    inline constexpr result_t(const result_t<void, E> &other) : is_value(other.is_value) {
        if (!is_value) {
            error = other.error;
        }
    }

    inline constexpr bool is_ok() const {
        return is_value;
    }

    inline constexpr bool is_err() const {
        return !is_value;
    }

    inline constexpr E unwrap_err() const {
        return error;
    }
};

// results can be tried similar to rust's `?` operator
// but how?

// flagset
// i think that enums as flags are usually really annoying. Therefore,
// this is a simple flagset implementation which backbones onto a enum
// of the defined flags.
template <typename T>
struct flagset {

    flagset(T value) : flags((underlying_t)value) {}

    flagset operator|(T flag) const {
        flagset result = *this;
        result.flags.set((underlying_t)flag);
        return result;
    }

    flagset operator|(flagset<T> o) const {
        flagset result = *this;
        result.flags |= o.flags;
        return result;
    }

    flagset operator&(flagset<T> o) const {
        flagset result = *this;
        result.flags &= o.flags;
        return result;
    }

    flagset &operator|=(T flag) {
        flags.set((underlying_t)flag);
        return *this;
    }

    flagset &operator|=(flagset<T> o) {
        flags |= o.flags;
        return *this;
    }

    flagset &operator&=(flagset<T> o) {
        flags &= o.flags;
        return *this;
    }

    flagset operator~() const {
        flagset result = *this;
        result.flags.flip();
        return result;
    }

    flagset set(T flag) const {
        flagset result = *this;
        result.flags.set((underlying_t)flag);
        return result;
    }

    flagset unset(T flag) const {
        flagset result = *this;
        result.flags.reset((underlying_t)flag);
        return result;
    }

    bool test(T flag) const {
        return flags.test((underlying_t)flag);
    }

    operator bool() const {
        return flags.any();
    }

    bool operator == (flagset<T> o) const {
        return flags == o.flags;
    }
private:
    using underlying_t = std::underlying_type_t<T>;
    // note: this will cause the set to be slightly larger than the enum,
    // but equal to the underlying type of the enum
    std::bitset<sizeof(underlying_t) * 8> flags;
};

template <typename T>
std::enable_if_t<std::is_enum_v<T>, flagset<T>> operator|(T &flag, T &flag2) {
    flagset<T> result;
    result |= flag;
    result |= flag2;
    return result;
}

template <typename T>
class ref_t {
public:
    constexpr inline ref_t() : ptr(nullptr), ref_count(nullptr) {}

    constexpr inline ref_t(std::nullptr_t) : ptr(nullptr), ref_count(nullptr) {}

    constexpr inline ref_t(const ref_t<T>& other) {
        ptr = other.ptr;
        if (ptr) {
            ref_count = other.ref_count;
            (*ref_count)++;
        }
    }

    constexpr inline ref_t(ref_t<T>&& other) {
        ptr = other.ptr;
        ref_count = other.ref_count;
        other.ptr = nullptr;
        other.ref_count = nullptr;
    }

    constexpr inline ref_t<T>& operator=(const ref_t<T>& other) {
        if (this != &other) {
            if (ptr) {
                (*ref_count)--;
                if (*ref_count == 0) {
                    delete ptr;
                    delete ref_count;
                }
            }
            ptr = other.ptr;
            ref_count = other.ref_count;
            if (ptr)
                (*ref_count)++;
        }
        return *this;
    }

    constexpr inline ref_t<T>& operator=(ref_t<T>&& other) {
        if (this != &other) {
            if (ptr) {
                (*ref_count)--;
                if (*ref_count == 0) {
                    delete ptr;
                    delete ref_count;
                }
            }
            ptr = other.ptr;
            ref_count = other.ref_count;
            other.ptr = nullptr;
            other.ref_count = nullptr;
        }
        return *this;
    }

    constexpr inline T& operator*() {
        return *ptr;
    }

    constexpr inline T* operator->() {
        assert(ptr);
        return ptr;
    }

    constexpr inline T const& operator*() const {
        return *ptr;
    }

    constexpr inline T const* operator->() const {
        return ptr;
    }

    constexpr inline ~ref_t() {
        if (ptr) {
            (*ref_count)--;
            if (*ref_count == 0) {
                delete ptr;
                delete ref_count;
            }
        }
    }

    constexpr bool operator==(const ref_t<T>& other) const {
        return ptr == other.ptr;
    }

    constexpr bool operator==(const std::nullptr_t) const {
        return ptr == nullptr;
    }

private:
    T *ptr;
    u32 *ref_count;

    template <typename U, typename ...Args>
    friend ref_t<U> make_ref(Args... args);

    template <typename U, typename ...Args>
    friend ref_t<U> make_ref_owned(Args... args);
};

template <typename T, typename ...Args>
ref_t<T> make_ref(Args... args) {
    ref_t<T> ref;
    ref.ptr = new T(args...);
    ref.ref_count = new u32(1);
    return ref;
}

// this creates a reference which will be handed over to others,
// but will be deleted when the last reference is gone
template <typename T, typename ...Args>
ref_t<T> make_ref_owned(Args... args) {
    ref_t<T> ref;
    ref.ptr = new T(args...);
    ref.ref_count = new u32(0);
    return ref;
}

// common os functions
struct file_t {
    slice<byte> contents;
    bool is_mmaped;
};
result_t<file_t, std::string> file_read(const char* path);

void file_close(file_t &file);

template <typename A>
A min(A a, A b) {
    return a < b ? a : b;
}

template <typename A>
A max(A a, A b) {
    return a > b ? a : b;
}

template <typename A>
A clamp(A a, A min, A max) {
    return a < min ? min : (a > max ? max : a);
}

__attribute__((noreturn))
void _panic_impl();

template <typename ...Args>
__attribute__((noreturn))
inline void panic(fmt::format_string<Args...> fmt, Args... args) {
    std::cout << std::endl << "panic! " << fmt::format(fmt, std::forward<Args>(args)...) << std::endl << std::endl;
    _panic_impl();
}

template <typename T>
void assert(T t) {
    if (!t) {
        panic("assertion failed!");
    }
}

// @note: this REALLY is not spec-compliant, but it's good enough for now.
// accuracy? I don't know him.
f32 str2f32(slice<byte> str);
u32 str2u32(slice<byte> str);
