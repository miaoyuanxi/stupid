/********************************************************
 * Description : connection base class
 * Data        : 2014-07-05 12:39:45
 * Author      : yanrk
 * Email       : feeling_dxl@yeah.net & ken_scott@163.com
 * Blog        : blog.csdn.net/cxxmaker
 * Version     : 1.0
 * History     :
 * Copyright(C): 2013 - 2015
 ********************************************************/

#ifndef STUPID_NET_CONNECTION_BASE_H
#define STUPID_NET_CONNECTION_BASE_H


#include "net/common/common.h"

NAMESPACE_STUPID_NET_BEGIN

class STUPID_API TcpConnectionBase
{
public:
    TcpConnectionBase();
    virtual ~TcpConnectionBase();

public:
    void set_user_data(void * user_data);
    void * get_user_data();

public:
    virtual size_t recv_buffer_size() = 0;
    virtual bool recv_buffer_copy_len(char * buffer, size_t length) = 0;
    virtual bool recv_buffer_move_len(char * buffer, size_t length) = 0;
    virtual bool recv_buffer_drop_len(size_t length) = 0;
    virtual void recv_buffer_water_mark(size_t length) = 0;
    virtual bool send_buffer_fill_len(const char * data, size_t length) = 0;
    virtual void close() = 0;

private:
    void                      * m_user_data;
};

NAMESPACE_STUPID_NET_END


#endif // STUPID_NET_CONNECTION_BASE_H