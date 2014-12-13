/********************************************************
 * Description : locker classes in windows
 * Data        : 2013-05-20 09:02:33
 * Author      : yanrk
 * Email       : yanrkchina@hotmail.com
 * Blog        : blog.csdn.net/cxxmaker
 * Version     : 1.0
 * History     :
 * Copyright(C): 2013 - 2015
 ********************************************************/

#ifdef _MSC_VER


#include <cassert>
#include "base/locker/windows_locker.h"

NAMESPACE_STUPID_BASE_BEGIN

WindowsThreadLocker::WindowsThreadLocker(const char * name)
    : m_name(nullptr != name ? name : "")
    , m_locker()
{
    InitializeCriticalSection(&m_locker);
}

WindowsThreadLocker::~WindowsThreadLocker()
{
    DeleteCriticalSection(&m_locker);
}

bool WindowsThreadLocker::acquire()
{
    EnterCriticalSection(&m_locker);
    return(true);
}

void WindowsThreadLocker::release()
{
    LeaveCriticalSection(&m_locker);
}

bool WindowsThreadLocker::try_acquire()
{
    return(false);
}


WindowsProcessLocker::WindowsProcessLocker(const char * name)
    : m_name(nullptr != name ? name : "")
    , m_locker(CreateMutex(nullptr, FALSE, name))
{
    assert(nullptr != m_locker);
}

WindowsProcessLocker::~WindowsProcessLocker()
{
    CloseHandle(m_locker);
}

bool WindowsProcessLocker::acquire()
{
    return(WAIT_OBJECT_0 == WaitForSingleObject(m_locker, INFINITE));
}

void WindowsProcessLocker::release()
{
    ReleaseMutex(m_locker);
}

bool WindowsProcessLocker::try_acquire()
{
    return(WAIT_OBJECT_0 == WaitForSingleObject(m_locker, 0));
}

NAMESPACE_STUPID_BASE_END


#endif // _MSC_VER
