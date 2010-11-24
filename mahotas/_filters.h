extern "C" {
    #include <Python.h>
    #include <numpy/ndarrayobject.h>
}

#include "numpypp/array.hpp"

/* The different boundary conditions. The mirror condition is not used
     by the python code, but C code is kept around in case we might wish
     to add it. */
typedef enum {
    EXTEND_FIRST = 0,
    EXTEND_NEAREST = 0,
    EXTEND_WRAP = 1,
    EXTEND_REFLECT = 2,
    EXTEND_MIRROR = 3,
    EXTEND_CONSTANT = 4,
    EXTEND_LAST = EXTEND_CONSTANT,
    EXTEND_DEFAULT = EXTEND_MIRROR
} ExtendMode;

int init_filter_offsets(PyArrayObject *array, bool *footprint,
         const npy_intp * const fshape, npy_intp* origins,
         const ExtendMode mode, npy_intp **offsets, npy_intp *border_flag_value,
         npy_intp **coordinate_offsets);

template <typename T>
struct filter_iterator {
    /* Move to the next point in an array, possible changing the pointer
         to the filter offsets when moving into a different region in the
         array: */
    filter_iterator(PyArrayObject* array, PyArrayObject* filter, ExtendMode mode = EXTEND_NEAREST, bool compress=true)
        :filter_data_(reinterpret_cast<const T* const>(PyArray_DATA(filter)))
        ,own_filter_data_(false)
        ,offsets_(0)
        ,coordinate_offsets_(0)
    {
        numpy::aligned_array<T> filter_array(filter);
        const npy_intp filter_size = filter_array.size();
        bool* footprint = 0;
        if (compress) {
            footprint = new bool[filter_size];
            typename numpy::aligned_array<T>::iterator fiter = filter_array.begin();
            for (int i = 0; i != filter_size; ++i, ++fiter) {
                footprint[i] = bool(*fiter);
            }
        }
        size_ = init_filter_offsets(array, footprint, PyArray_DIMS(filter), 0,
                    mode, &offsets_, &border_flag_value_, 0);
        if (compress) {
            int j = 0;
            T* new_filter_data = new T[size_];
            typename numpy::aligned_array<T>::iterator fiter = filter_array.begin();
            for (int i = 0; i != filter_size; ++i, ++fiter) {
                if (*fiter) {
                    new_filter_data[j++] = *fiter;
                }
            }
            filter_data_ = new_filter_data;
            own_filter_data_ = true;
            delete [] footprint;
        }

        cur_offsets_ = offsets_;
        nd_ = PyArray_NDIM(array);
        //init_filter_boundaries(array, filter, minbound_, maxbound_);
        for (int i = 0; i != PyArray_NDIM(filter); ++i) {
            minbound_[i] = 1;
            maxbound_[i] = PyArray_DIM(array,i) - 2;
        }
        this->strides_[this->nd_ - 1] = size_;
        for (int i = nd_ - 2; i >= 0; --i) {
            const npy_intp step = std::min<npy_intp>(PyArray_DIM(filter, i + 1), PyArray_DIM(array, i + 1));
            this->strides_[i] = this->strides_[i + 1] * step;
        }
        for (int i = 0; i < this->nd_; ++i) {
            const npy_intp step = std::min<npy_intp>(PyArray_DIM(array, i), PyArray_DIM(filter, i));
            this->backstrides_[i] = (step - 1) * this->strides_[i];
        }
    }
    ~filter_iterator() {
        delete [] offsets_;
        if (coordinate_offsets_) delete coordinate_offsets_;
        if (own_filter_data_) delete [] filter_data_;
    }
    template <typename OtherIterator>
    void iterate_with(const OtherIterator& iterator) {
        for (int i = nd_ - 1; i >= 0; --i) {
            npy_intp p = iterator.index(i);
            if (p < (iterator.dimension(i) - 1)) {
                if (p < this->minbound_[i] || p >= this->maxbound_[i]) {
                    this->cur_offsets_ += this->strides_[i];
                }
                return;
            }
            this->cur_offsets_ -= this->backstrides_[i];
            assert( (this->cur_offsets_ - this->offsets_) >= 0);
        }
    }
    template <typename OtherIterator>
    bool retrieve(const OtherIterator& iterator, const npy_intp j, T& array_val) {
        if (this->cur_offsets_[j] == border_flag_value_) return false;
        array_val = *( (&*iterator) + this->cur_offsets_[j]);
        return true;
    }
    template <typename OtherIterator>
    void set(const OtherIterator& iterator, npy_intp j, const T& val) {
        *( (&*iterator) + this->cur_offsets_[j]) = val;
    }

    const T& operator [] (const npy_intp j) const { return filter_data_[j]; }
    npy_intp size() const { return size_; }
    private:
        const T* filter_data_;
        bool own_filter_data_;
        npy_intp* cur_offsets_;
        npy_intp size_;
        npy_intp nd_;
        npy_intp* offsets_;
        npy_intp* coordinate_offsets_;
        npy_intp strides_[NPY_MAXDIMS];
        npy_intp backstrides_[NPY_MAXDIMS];
        npy_intp minbound_[NPY_MAXDIMS];
        npy_intp maxbound_[NPY_MAXDIMS];
        npy_intp border_flag_value_;
};

