/*
 * =====================================================================================
 *
 *       Filename:  ptr.h
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  10/12/2015 10:57:53 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  DAI ZHENGHUA (), djx.zhenghua@gmail.com
 *        Company:  
 *
 * =====================================================================================
 */

#ifndef  SMART_PTR_INC
#define  SMART_PTR_INC

template<typename T>
class user_ptr{
    public:
        user_ptr(T* p):pointer(p){}
        user_ptr():pointer(nullptr){}
        T* operator->(){return pointer;}
         operator T*() {return pointer;}
         user_ptr& operator=(user_ptr o){ pointer = o.pointer; return *this;}
         user_ptr& operator=(T* o){ pointer = o; return *this;}
         explicit operator bool()const noexcept { return pointer != NULL;}
    private:
        T* pointer;
};

#endif   /* ----- #ifndef SMART_PTR_INC  ----- */
