//===---------------------SharingPtr.h --------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef utility_SharingPtr_h_
#define utility_SharingPtr_h_

#include <algorithm>

namespace lldb_private {

namespace imp {

class shared_count
{
    shared_count(const shared_count&);
    shared_count& operator=(const shared_count&);

protected:
    long shared_owners_;
    virtual ~shared_count();
private:
    virtual void on_zero_shared() = 0;

public:
    explicit shared_count(long refs = 0)
        : shared_owners_(refs) {}

    void add_shared();
    void release_shared();
    long use_count() const {return shared_owners_ + 1;}
};

template <class T>
class shared_ptr_pointer
    : public shared_count
{
    T data_;
public:
    shared_ptr_pointer(T p)
        :  data_(p) {}

private:
    virtual void on_zero_shared();
};

template <class T>
void
shared_ptr_pointer<T>::on_zero_shared()
{
    delete data_;
}

template <class T>
class shared_ptr_emplace
    : public shared_count
{
    T data_;
public:

    shared_ptr_emplace()
        :  data_() {}

    template <class A0>
        shared_ptr_emplace(A0& a0)
            :  data_(a0) {}

    template <class A0, class A1>
        shared_ptr_emplace(A0& a0, A1& a1)
            :  data_(a0, a1) {}

    template <class A0, class A1, class A2>
        shared_ptr_emplace(A0& a0, A1& a1, A2& a2)
            :  data_(a0, a1, a2) {}

    template <class A0, class A1, class A2, class A3>
        shared_ptr_emplace(A0& a0, A1& a1, A2& a2, A3& a3)
            :  data_(a0, a1, a2, a3) {}

    template <class A0, class A1, class A2, class A3, class A4>
        shared_ptr_emplace(A0& a0, A1& a1, A2& a2, A3& a3, A4& a4)
            :  data_(a0, a1, a2, a3, a4) {}

private:
    virtual void on_zero_shared();
public:
    T* get() {return &data_;}
};

template <class T>
void
shared_ptr_emplace<T>::on_zero_shared()
{
}

}  // namespace

template<class T>
class SharingPtr
{
public: 
    typedef T element_type; 
private:
    element_type*      ptr_;
    imp::shared_count* cntrl_;

    struct nat {int for_bool_;};
public:
    SharingPtr();
    template<class Y> explicit SharingPtr(Y* p);
    template<class Y> SharingPtr(const SharingPtr<Y>& r, element_type *p); 
    SharingPtr(const SharingPtr& r);
    template<class Y>
        SharingPtr(const SharingPtr<Y>& r);

    ~SharingPtr();

    SharingPtr& operator=(const SharingPtr& r); 
    template<class Y> SharingPtr& operator=(const SharingPtr<Y>& r); 

    void swap(SharingPtr& r);
    void reset();
    template<class Y> void reset(Y* p);

    element_type* get() const {return ptr_;}
    element_type& operator*() const {return *ptr_;}
    element_type* operator->() const {return ptr_;}
    long use_count() const {return cntrl_ ? cntrl_->use_count() : 0;}
    bool unique() const {return use_count() == 1;}
    bool empty() const {return cntrl_ == 0;}
    operator nat*() const {return (nat*)get();}

    static SharingPtr<T> make_shared();

    template<class A0>
        static SharingPtr<T> make_shared(A0&);

    template<class A0, class A1>
        static SharingPtr<T> make_shared(A0&, A1&);

    template<class A0, class A1, class A2>
        static SharingPtr<T> make_shared(A0&, A1&, A2&);

    template<class A0, class A1, class A2, class A3>
        static SharingPtr<T> make_shared(A0&, A1&, A2&, A3&);

    template<class A0, class A1, class A2, class A3, class A4>
        static SharingPtr<T> make_shared(A0&, A1&, A2&, A3&, A4&);

private:

    template <class U> friend class SharingPtr;
};

template<class T>
inline
SharingPtr<T>::SharingPtr()
    : ptr_(0),
      cntrl_(0)
{
}

template<class T>
template<class Y>
SharingPtr<T>::SharingPtr(Y* p)
    : ptr_(p)
{
    std::auto_ptr<Y> hold(p);
    typedef imp::shared_ptr_pointer<Y*> _CntrlBlk;
    cntrl_ = new _CntrlBlk(p);
    hold.release();
}

template<class T>
template<class Y>
inline
SharingPtr<T>::SharingPtr(const SharingPtr<Y>& r, element_type *p)
    : ptr_(p),
      cntrl_(r.cntrl_)
{
    if (cntrl_)
        cntrl_->add_shared();
}

template<class T>
inline
SharingPtr<T>::SharingPtr(const SharingPtr& r)
    : ptr_(r.ptr_),
      cntrl_(r.cntrl_)
{
    if (cntrl_)
        cntrl_->add_shared();
}

template<class T>
template<class Y>
inline
SharingPtr<T>::SharingPtr(const SharingPtr<Y>& r)
    : ptr_(r.ptr_),
      cntrl_(r.cntrl_)
{
    if (cntrl_)
        cntrl_->add_shared();
}

template<class T>
SharingPtr<T>::~SharingPtr()
{
    if (cntrl_)
        cntrl_->release_shared();
}

template<class T>
inline
SharingPtr<T>&
SharingPtr<T>::operator=(const SharingPtr& r)
{
    SharingPtr(r).swap(*this);
    return *this;
}

template<class T>
template<class Y>
inline
SharingPtr<T>&
SharingPtr<T>::operator=(const SharingPtr<Y>& r)
{
    SharingPtr(r).swap(*this);
    return *this;
}

template<class T>
inline
void
SharingPtr<T>::swap(SharingPtr& r)
{
    std::swap(ptr_, r.ptr_);
    std::swap(cntrl_, r.cntrl_);
}

template<class T>
inline
void
SharingPtr<T>::reset()
{
    SharingPtr().swap(*this);
}

template<class T>
template<class Y>
inline
void
SharingPtr<T>::reset(Y* p)
{
    SharingPtr(p).swap(*this);
}

template<class T>
SharingPtr<T>
SharingPtr<T>::make_shared()
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk();
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
template<class A0>
SharingPtr<T>
SharingPtr<T>::make_shared(A0& a0)
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk(a0);
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
template<class A0, class A1>
SharingPtr<T>
SharingPtr<T>::make_shared(A0& a0, A1& a1)
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk(a0, a1);
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
template<class A0, class A1, class A2>
SharingPtr<T>
SharingPtr<T>::make_shared(A0& a0, A1& a1, A2& a2)
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk(a0, a1, a2);
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
template<class A0, class A1, class A2, class A3>
SharingPtr<T>
SharingPtr<T>::make_shared(A0& a0, A1& a1, A2& a2, A3& a3)
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk(a0, a1, a2, a3);
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
template<class A0, class A1, class A2, class A3, class A4>
SharingPtr<T>
SharingPtr<T>::make_shared(A0& a0, A1& a1, A2& a2, A3& a3, A4& a4)
{
    typedef imp::shared_ptr_emplace<T> CntrlBlk;
    SharingPtr<T> r;
    r.cntrl_ = new CntrlBlk(a0, a1, a2, a3, a4);
    r.ptr_ = static_cast<CntrlBlk*>(r.cntrl_)->get();
    return r;
}

template<class T>
inline
SharingPtr<T>
make_shared()
{
    return SharingPtr<T>::make_shared();
}

template<class T, class A0>
inline
SharingPtr<T>
make_shared(A0& a0)
{
    return SharingPtr<T>::make_shared(a0);
}

template<class T, class A0, class A1>
inline
SharingPtr<T>
make_shared(A0& a0, A1& a1)
{
    return SharingPtr<T>::make_shared(a0, a1);
}

template<class T, class A0, class A1, class A2>
inline
SharingPtr<T>
make_shared(A0& a0, A1& a1, A2& a2)
{
    return SharingPtr<T>::make_shared(a0, a1, a2);
}

template<class T, class A0, class A1, class A2, class A3>
inline
SharingPtr<T>
make_shared(A0& a0, A1& a1, A2& a2, A3& a3)
{
    return SharingPtr<T>::make_shared(a0, a1, a2, a3);
}

template<class T, class A0, class A1, class A2, class A3, class A4>
inline
SharingPtr<T>
make_shared(A0& a0, A1& a1, A2& a2, A3& a3, A4& a4)
{
    return SharingPtr<T>::make_shared(a0, a1, a2, a3, a4);
}


template<class T, class U>
inline
bool
operator==(const SharingPtr<T>& __x, const SharingPtr<U>& __y)
{
    return __x.get() == __y.get();
}

template<class T, class U>
inline
bool
operator!=(const SharingPtr<T>& __x, const SharingPtr<U>& __y)
{
    return !(__x == __y);
}

template<class T, class U>
inline
bool
operator<(const SharingPtr<T>& __x, const SharingPtr<U>& __y)
{
    return __x.get() < __y.get();
}

template<class T>
inline
void
swap(SharingPtr<T>& __x, SharingPtr<T>& __y)
{
    __x.swap(__y);
}

template<class T, class U>
inline
SharingPtr<T>
static_pointer_cast(const SharingPtr<U>& r)
{
    return SharingPtr<T>(r, static_cast<T*>(r.get()));
}

template<class T, class U>
SharingPtr<T>
const_pointer_cast(const SharingPtr<U>& r)
{
    return SharingPtr<T>(r, const_cast<T*>(r.get()));
}

} // namespace lldb

#endif  // utility_SharingPtr_h_
