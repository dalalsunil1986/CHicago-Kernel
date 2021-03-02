/* File author is Ítalo Lima Marconato Matias
 *
 * Created on March 01 of 2021, at 11:59 BRT
 * Last edited on March 02 of 2021, at 13:21 BRT */

#pragma once

#include <types.hxx>

namespace CHicago {

template<class T> struct RemoveReference { typedef T Value; };
template<class T> struct RemoveReference<T&> { typedef T Value; };
template<class T> struct RemoveReference<T&&> { typedef T Value; };

/* Forward, Move and Exchange are compiler black magic used for making our life a bit easier when, for example, we want
 * to implement move operators/constructors, or force a value to be moved instead of copied. */

template<class T> static inline constexpr T &&Forward(typename RemoveReference<T>::Value &Value) {
    return static_cast<T&&>(Value);
}

template<class T> static inline constexpr T &&Forward(typename RemoveReference<T>::Value &&Value) {
    return static_cast<T&&>(Value);
}

template<class T> static inline constexpr typename RemoveReference<T>::Value &&Move(T &&Value) {
    return static_cast<typename RemoveReference<T>::Value&&>(Value);
}

template<class T, class U = T> static inline constexpr T Exchange(T &Old, U &&New) {
    T old = Move(Old);
    return Old = Forward<U>(New), old;
}

template<class T> static inline constexpr Void Swap(T &Left, T &Right) {
    T tmp = Move(Left);
    Left = Move(Right), Right = Move(tmp);
}

/* The reverse iterator/wrapper struct are just so we can easily iterate on reverse (duh). */

template<typename T, typename U> class ReverseIterator {
public:
    class Iterator {
    public:
        Iterator(U Value) : Value(Value) { }
        T &operator *(Void) { return *Value; }
        Iterator &operator ++(Void) { return Value--, *this; }
        Boolean operator ==(const Iterator &Value) const { return this->Value == Value.Value; }
    private:
        U Value;
    };

    ReverseIterator(U Start, U End) : Start(Start), End(End) { this->Start--, this->End--; }

    inline auto begin(Void) { return Iterator(End); }
    inline auto end(Void) { return Iterator(Start); }
private:
    U Start, End;
};

template<typename T, typename U> class ConstReverseIterator {
public:
    class Iterator {
    public:
        Iterator(U Value) : Value(Value) { }
        T operator *(Void) const { return *Value; }
        Iterator &operator ++(Void) { return Value--, *this; }
        Boolean operator ==(const Iterator &Value) const { return this->Value == Value.Value; }
    private:
        U Value;
    };

    ConstReverseIterator(U Start, U End) : Start(Start), End(End) { this->Start--, this->End--; }

    inline auto begin(Void) const { return Iterator(End); }
    inline auto end(Void) const { return Iterator(Start); }
private:
    U Start, End;
};

}
