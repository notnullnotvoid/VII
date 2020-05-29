#ifndef LIST_HPP
#define LIST_HPP

#include <stdlib.h>
#include <assert.h>
#include <string.h>

//a simple array list implementation
//NOTE: this struct zero-initializes to a valid state!
//      List<T> list = {}; //this is valid
template <typename TYPE>
struct List {
    TYPE * data;
    size_t len;
    size_t max;

    void init(size_t reserve = 1024 / sizeof(TYPE) + 1) {
        assert(reserve > 0);
        data = (TYPE *) malloc(reserve * sizeof(TYPE));
        max = reserve;
        len = 0;
    }

    void add(TYPE t) {
        if (len == max) {
            max = max * 2 + 1;
            data = (TYPE *) realloc(data, max * sizeof(TYPE));
        }

        data[len] = t;
        ++len;
    }

    template <typename ALLOC>
    void add(TYPE t, ALLOC & alloc) {
        if (len == max) {
            max = max * 2 + 1;
            data = (TYPE *) alloc.realloc(data, len * sizeof(TYPE), max * sizeof(TYPE), alignof(TYPE));
        }

        data[len] = t;
        ++len;
    }

    //TODO: create unsafe_add() method (that doesn't do the resize check) and use it where relevant

    void remove(size_t index) {
        assert(index < len);
        --len;
        for (int i = index; i < len; ++i) {
            data[i] = data[i + 1];
        }
    }

    //removes elements in range [first, last)
    void remove(size_t first, size_t lastPlusOne) {
        assert(first < lastPlusOne);
        assert(lastPlusOne <= len);
        size_t range = lastPlusOne - first;
        len -= range;
        for (int i = first; i < len; ++i) {
            data[i] = data[i + range];
        }
    }

    void shrink_to_fit() {
        data = (TYPE *) realloc(data, len * sizeof(TYPE));
    }

    List<TYPE> clone() {
        List<TYPE> ret = { (TYPE *) malloc(len * sizeof(TYPE)), len, len };
        memcpy(ret.data, data, len * sizeof(TYPE));
        return ret;
    }

    void finalize() {
        free(data);
        *this = {};
    }

    TYPE & operator[](size_t index) {
    	assert(index < len);
        return data[index];
    }

    //no need to define an iterator class, because range-for will work with raw pointers
    TYPE * begin() { return { data }; }
    TYPE * end() { return { data + len }; }
};

//convenience function for same-line declaration+initialization
template<typename TYPE>
static inline List<TYPE> create_list(size_t reserve = 1024 / sizeof(TYPE) + 1) {
    List<TYPE> list;
    list.init(reserve);
    return list;
}

#endif
