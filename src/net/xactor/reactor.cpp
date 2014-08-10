/********************************************************
 * Description : linux tcp connection reactor class
 * Data        : 2014-07-01 18:07:20
 * Author      : yanrk
 * Email       : feeling_dxl@yeah.net & ken_scott@163.com
 * Blog        : blog.csdn.net/cxxmaker
 * Version     : 1.0
 * History     :
 * Copyright(C): 2013 - 2015
 ********************************************************/

#ifndef _MSC_VER


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
#include "net/xactor/reactor.h"
#include "net/xactor/manager.h"
#include "net/utility/net_switch.h"
#include "net/utility/tcp.h"
#include "net/utility/utility.h"
#include "base/log/log.h"
#include "base/time/time.h"
#include "base/utility/guard.h"
#include "base/utility/singleton.h"

NAMESPACE_STUPID_NET_BEGIN

struct REACTOR_THREAD_PARAM
{
    TcpReactor & tcp_reactor;
    size_t       thread_index;

    REACTOR_THREAD_PARAM(TcpReactor & reactor, size_t index)
        : tcp_reactor(reactor), thread_index(index)
    {

    }
};

thread_return_t STUPID_STDCALL connection_thread_process(thread_argument_t param)
{
    if (nullptr != param)
    {
        REACTOR_THREAD_PARAM * thread_param = reinterpret_cast<REACTOR_THREAD_PARAM *>(param);
        thread_param->tcp_reactor.reactor_connection_process();
        STUPID_DEL(thread_param);
    }
    return(THREAD_DEFAULT_RET);
}

thread_return_t STUPID_STDCALL data_thread_process(thread_argument_t param)
{
    if (nullptr != param)
    {
        REACTOR_THREAD_PARAM * thread_param = reinterpret_cast<REACTOR_THREAD_PARAM *>(param);
        thread_param->tcp_reactor.reactor_data_process(thread_param->thread_index);
        STUPID_DEL(thread_param);
    }
    return(THREAD_DEFAULT_RET);
}

thread_return_t STUPID_STDCALL business_thread_process(thread_argument_t param)
{
    if (nullptr != param)
    {
        REACTOR_THREAD_PARAM * thread_param = reinterpret_cast<REACTOR_THREAD_PARAM *>(param);
        thread_param->tcp_reactor.reactor_business_process(thread_param->thread_index);
        STUPID_DEL(thread_param);
    }
    return(THREAD_DEFAULT_RET);
}

enum business_event_enum
{
    connect_notify, 
    accept_notify, 
    recv_notify, 
    send_notify, 
    close_notify 
};

TcpReactor::TcpReactor()
    : m_running(false)
    , m_manager(nullptr)
    , m_epoll(-1)
    , m_listeners()
    , m_thread(nullptr)
    , m_thread_count(0)
    , m_block_pool()
    , m_data_events_list_vector()
    , m_data_events_list_vector_locker()
    , m_business_event_list_vector()
    , m_business_event_list_vector_locker()
    , m_normal_connection_set()
    , m_closed_connection_set()
    , m_connection_set_locker()
    , m_unique_creator(0, static_cast<size_t>(~0))
    , m_binded_connection_set()
    , m_binded_connection_set_locker()
{
    Stupid::Base::Singleton<NetSwitch>::instance().work();
}

TcpReactor::~TcpReactor()
{
    exit();
}

bool TcpReactor::init(TcpManager * manager, size_t event_thread_count, size_t handle_thread_count, unsigned short * service_port, size_t service_port_count)
{
    if (nullptr == manager)
    {
        RUN_LOG_CRI("tcp manager is nullptr");
        return(false);
    }

    exit();

    m_running = true;

    m_manager = manager;

    if (!create_epoll())
    {
        return(false);
    }
    else
    {
        RUN_LOG_DBG("create epoll success");
    }

    if (!create_listener(service_port, service_port_count))
    {
        return(false);
    }
    else if (0 != service_port)
    {
        RUN_LOG_DBG("create listener success");
    }

    if (!acquire_reactor_threads(event_thread_count, handle_thread_count))
    {
        return(false);
    }
    else
    {
        RUN_LOG_DBG("create reactor threads success");
    }

    RUN_LOG_DBG("init tcp reactor success");

    return(true);
}

void TcpReactor::exit()
{
    if (!m_running)
    {
        return;
    }

    m_running = false;

    destroy_epoll();
    release_reactor_threads();
    destroy_listener();
    destroy_connections();
    clear_data_events();
    clear_business_event();

    m_block_pool.clear();
    m_unique_creator.reset();

    m_manager = nullptr;

    RUN_LOG_DBG("exit tcp reactor success");
}

bool TcpReactor::create_connection(const sockaddr_in_t & server_address, size_t identity, unsigned short bind_port)
{
    return(do_connect(server_address, identity, bind_port));
}

void TcpReactor::connection_send(TcpConnection * connection)
{
    /* when send-buffer has data, the connection should continue to send */
    modify_connection_of_epoll(connection, true, true);
}

void TcpReactor::close_connection(TcpConnection * connection)
{
    /* binded-set make sure we will not close connection more times */
    if (delete_connection_from_epoll(connection))
    {
        connection->set_error();
        BusinessEvent business_event;
        business_event.connection = connection;
        business_event.event = close_notify;
        append_business_event(business_event);
        connection->increase_reference();
    }
}

bool TcpReactor::running() const
{
    return(m_running);
}

bool TcpReactor::create_listener(unsigned short * service_port, size_t service_port_count)
{
    if (nullptr == service_port || 0 == service_port_count)
    {
        return(true);
    }

    for (size_t service_port_index = 0; service_port_index < service_port_count; ++service_port_index)
    {
        unsigned short port = service_port[service_port_index];
        if (0 == port)
        {
            continue;
        }

        TcpConnection * connection = acquire_connection();
        if (nullptr == connection)
        {
            RUN_LOG_ERR("acquire connection failed: %d", stupid_system_error());
            return(false);
        }

        socket_t listener = BAD_SOCKET;
        if (!tcp_listen(port, listener, 1024))
        {
            release_connection(connection);
            return(false);
        }

        tcp_set_block_switch(listener, false);

        connection->set_socket(listener);

        insert_connection(connection);

        if (!append_connection_to_epoll(connection))
        {
            remove_connection(connection);
            return(false);
        }

        connection->set_listener(listener);
        connection->set_listener_port(port);

        m_listeners.push_back(connection);
    }

    return(true);
}

void TcpReactor::destroy_listener()
{
    ConnectionVector::iterator iter = m_listeners.begin();
    while (m_listeners.end() != iter)
    {
        delete_connection_from_epoll(*iter);
        ++iter;
    }
    m_listeners.clear();
}

bool TcpReactor::create_epoll()
{
    const int max_connect_count = 99999; /* maybe it is disable */
    m_epoll = epoll_create(max_connect_count);
    if (-1 == m_epoll)
    {
        RUN_LOG_CRI("create epoll failed: %d", stupid_net_error());
        return(false);
    }
    return(true);
}

void TcpReactor::destroy_epoll()
{
    if (-1 != m_epoll)
    {
        close(m_epoll);
        m_epoll = -1;
    }
}

bool TcpReactor::append_connection_to_epoll(TcpConnection * connection)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> binded_connection_set_guard(m_binded_connection_set_locker);
    if (m_binded_connection_set.end() != m_binded_connection_set.find(connection))
    {
        return(false);
    }

    struct epoll_event event;
    event.data.ptr = connection;
    event.events = EPOLLIN | EPOLLET;
    if (-1 == epoll_ctl(m_epoll, EPOLL_CTL_ADD, connection->get_socket(), &event))
    {
        RUN_LOG_CRI("append connection to epoll failed: %d", stupid_net_error());
        return(false);
    }

    size_t unique_value = 0;
    m_unique_creator.acquire(unique_value);
    connection->set_unique(unique_value);

    m_binded_connection_set.insert(connection);
    connection->increase_reference();
    return(true);
}

bool TcpReactor::delete_connection_from_epoll(TcpConnection * connection)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> binded_connection_set_guard(m_binded_connection_set_locker);
    if (m_binded_connection_set.end() == m_binded_connection_set.find(connection))
    {
        return(false);
    }

    struct epoll_event event;
    event.data.ptr = connection;
    if (-1 == epoll_ctl(m_epoll, EPOLL_CTL_DEL, connection->get_socket(), &event))
    {
        RUN_LOG_CRI("delete connection from epoll failed: %d", stupid_net_error());
    }

    m_unique_creator.release(connection->get_unique());

    m_binded_connection_set.erase(connection);
    connection->decrease_reference();
    return(true);
}

bool TcpReactor::modify_connection_of_epoll(TcpConnection * connection, bool send, bool recv)
{
    struct epoll_event event;
    event.data.ptr = connection;
    event.events = EPOLLET;
    if (send)
    {
        event.events |= EPOLLOUT;
    }
    if (recv)
    {
        event.events |= EPOLLIN;
    }

    if (-1 == epoll_ctl(m_epoll, EPOLL_CTL_MOD, connection->get_socket(), &event))
    {
        RUN_LOG_CRI("modify connection of epoll failed: %d", stupid_net_error());
        return(false);
    }

    return(true);
}

bool TcpReactor::do_connect(const sockaddr_in_t & server_address, size_t identity, unsigned short bind_port)
{
    socket_t connecter = BAD_SOCKET;
    if (!tcp_connect(server_address, connecter, bind_port))
    {
        return(false);
    }

    tcp_set_block_switch(connecter, false);

    TcpConnection * connection = acquire_connection();
    if (nullptr == connection)
    {
        RUN_LOG_ERR("acquire connection failed: %d", stupid_system_error());
        tcp_close(connecter);
        return(false);
    }

    connection->set_socket(connecter);
    connection->set_connected(true);
    connection->set_requester(true);
    connection->set_address(server_address);
    connection->set_identity(identity);

    insert_connection(connection);

    if (!append_connection_to_epoll(connection))
    {
        remove_connection(connection);
        return(false);
    }

    BusinessEvent business_event;
    business_event.connection = connection;

    business_event.event = connect_notify;
    append_business_event(business_event);
    connection->increase_reference();

    business_event.event = send_notify;
    append_business_event(business_event);
    connection->increase_reference();

    return(true);
}

bool TcpReactor::do_accept(TcpConnection * listener_connection)
{
    size_t accept_count = 0;

    socket_t listener = listener_connection->get_listener();
    unsigned short listener_port = listener_connection->get_listener_port();

    while (true)
    {
        socket_t accepter = BAD_SOCKET;
        sockaddr_in_t client_address;
        sock_len_t address_len = sizeof(client_address);
        if (!tcp_accept(listener, accepter, &client_address, &address_len))
        {
            break;
        }

        tcp_set_block_switch(accepter, false);

        TcpConnection * connection = acquire_connection();
        if (nullptr == connection)
        {
            RUN_LOG_ERR("acquire connection failed: %d", stupid_system_error());
            tcp_close(accepter);
            break;
        }

        connection->set_socket(accepter);
        connection->set_listener(listener);
        connection->set_listener_port(listener_port);
        connection->set_connected(true);
        connection->set_requester(false);
        connection->set_address(client_address);

        insert_connection(connection);

        if (!append_connection_to_epoll(connection))
        {
            remove_connection(connection);
            break;
        }

        BusinessEvent business_event;
        business_event.connection = connection;

        business_event.event = accept_notify;
        append_business_event(business_event);
        connection->increase_reference();

        business_event.event = send_notify;
        append_business_event(business_event);
        connection->increase_reference();

        accept_count += 1;
    }

    return(0 != accept_count);
}

bool TcpReactor::do_recv(TcpConnection * connection)
{
    const int buff_siz = 1024 * 4;
    char buffer[buff_siz];

    while (true)
    {
        int recv_len = stupid_recv(connection->get_socket(), buffer, buff_siz);
        if (recv_len < 0)
        {
            if (stupid_is_net_blocking_error())
            {
                break;
            }
            else
            {
                RUN_LOG_ERR("do_recv failed: %d", stupid_net_error());
                return(false);
            }
        }
        else if (0 == recv_len)
        {
            DBG_LOG("do_recv failed: remote close");
            return(false);
        }
        else
        {
            if (!connection->recv_buffer_fill_len(buffer, recv_len))
            {
                RUN_LOG_ERR("do_recv failed: recv_buffer_fill_len error");
                return(false);
            }
        }
    }

    return(true);
}

bool TcpReactor::do_send(TcpConnection * connection)
{
    const int buff_siz = 1024 * 4;
    char buffer[buff_siz];

    while (true)
    {
        const int data_len = static_cast<int>(std::min<size_t>(buff_siz, connection->send_buffer_size()));
        if (0 == data_len)
        {
            DBG_LOG("do_send failed: no data");
            break;
        }
        if (!connection->send_buffer_copy_len(buffer, data_len))
        {
            RUN_LOG_ERR("do_send failed: send_buffer_copy_len error");
            return(false);
        }

        int send_len = stupid_send(connection->get_socket(), buffer, data_len);
        if (send_len < 0)
        {
            if (stupid_is_net_blocking_error())
            {
                break;
            }
            else
            {
                DBG_LOG("do_send failed: %d", stupid_net_error());
                return(false);
            }
        }
        else
        {
            if (!connection->send_buffer_drop_len(send_len))
            {
                RUN_LOG_ERR("do_send failed: send_buffer_drop_len error");
                return(false);
            }
            if (data_len != send_len)
            {
                break;
            }
        }
    }

    return(true);
}

void TcpReactor::do_close(TcpConnection * connection)
{
    close_connection(connection);
}

bool TcpReactor::handle_connect(TcpConnection * connection, size_t identity)
{
    return(m_manager->handle_connect(connection, identity));
}

bool TcpReactor::handle_accept(TcpConnection * connection, unsigned short listener_port)
{
    return(m_manager->handle_accept(connection, listener_port));
}

bool TcpReactor::handle_recv(TcpConnection * connection)
{
    return(m_manager->handle_recv(connection));
}

bool TcpReactor::handle_send(TcpConnection * connection)
{
    return(m_manager->handle_send(connection));
}

bool TcpReactor::handle_close(TcpConnection * connection)
{
    return(m_manager->handle_close(connection));
}

bool TcpReactor::acquire_reactor_threads(size_t event_thread_count, size_t handle_thread_count)
{
    const size_t connection_thread_count = 1;
    const size_t data_thread_count = (0 != event_thread_count ? event_thread_count : 10);
    const size_t business_thread_count = handle_thread_count;
    const size_t thread_count = connection_thread_count + data_thread_count + business_thread_count;

    STUPID_NEW_ARR(m_thread, Stupid::Base::Thread, thread_count);
    if (nullptr == m_thread)
    {
        RUN_LOG_CRI("new reactor thread array failed");
        return(false);
    }

    m_data_events_list_vector.resize(data_thread_count);
    m_business_event_list_vector.resize(business_thread_count);

    for (m_thread_count = 0; m_thread_count < thread_count; ++m_thread_count)
    {
        size_t thread_index = 0;
        thread_func_ptr_t reactor_thread_process = nullptr;
        if (m_thread_count < connection_thread_count)
        {
            reactor_thread_process = connection_thread_process;
            thread_index = m_thread_count + 1;
        }
        else if (m_thread_count - connection_thread_count < data_thread_count)
        {
            reactor_thread_process = data_thread_process;
            thread_index = m_thread_count - connection_thread_count + 1;
        }
        else
        {
            reactor_thread_process = business_thread_process;
            thread_index = m_thread_count - connection_thread_count - data_thread_count + 1;
        }

        REACTOR_THREAD_PARAM * reactor_thread_param = nullptr;
        STUPID_NEW(reactor_thread_param, REACTOR_THREAD_PARAM(*this, thread_index));
        if (nullptr == reactor_thread_param)
        {
            RUN_LOG_CRI("new reactor thread param failed");
            return(false);
        }

        Stupid::Base::Thread & reactor_thread = m_thread[m_thread_count];
        reactor_thread.set_thread_args(reactor_thread_process, reactor_thread_param);
        if (!reactor_thread.acquire())
        {
            RUN_LOG_CRI("create reactor thread failed: %d", stupid_system_error());
            STUPID_DEL(reactor_thread_param);
            return(false);
        }
    }

    return(true);
}

void TcpReactor::release_reactor_threads()
{
    for (size_t thread_index = 0; thread_index < m_thread_count; ++thread_index)
    {
        Stupid::Base::Thread & reactor_thread = m_thread[thread_index];
        reactor_thread.release();
    }
    STUPID_DEL_ARR(m_thread);
    m_thread_count = 0;
}

TcpConnection * TcpReactor::acquire_connection()
{
    TcpConnection * connection = nullptr;
    STUPID_NEW(connection, TcpConnection(*this, m_block_pool));
    return(connection);
}

void TcpReactor::release_connection(TcpConnection *& connection)
{
    STUPID_DEL(connection);
}

void TcpReactor::insert_connection(TcpConnection * connection)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
    m_normal_connection_set.insert(connection);
    if (connection->get_connected())
    {
        sockaddr_in_t remote_address = connection->get_address();
        if (connection->get_requester())
        {
            RUN_LOG_DBG("connect to server %s:%d, connection: %x", inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), connection);
        }
        else
        {
            RUN_LOG_DBG("client %s:%d connect, connection: %x", inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), connection);
        }
    }
}

void TcpReactor::remove_connection(TcpConnection *& connection)
{
    /*
     * when get to here, maybe connection->has_reference() is true
     * so we can not call release_connection(connection) here
     * but the connection will from normal-set transfer to closed-set
     */
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
    m_normal_connection_set.erase(connection);
    m_closed_connection_set.insert(connection);
    if (connection->get_connected())
    {
        sockaddr_in_t remote_address = connection->get_address();
        if (connection->get_requester())
        {
            RUN_LOG_DBG("disconnect from server %s:%d, connection: %x", inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), connection);
        }
        else
        {
            RUN_LOG_DBG("client %s:%d disconnect, connection: %x", inet_ntoa(remote_address.sin_addr), ntohs(remote_address.sin_port), connection);
        }
    }
}

void TcpReactor::destroy_invalid_connections()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
    ConnectionSet valid_connection_set;
    ConnectionSet::iterator iter = m_closed_connection_set.begin();
    while (m_closed_connection_set.end() != iter)
    {
        TcpConnection * connection = *iter;
        if (connection->has_reference())
        {
            valid_connection_set.insert(connection);
        }
        else
        {
            STUPID_DEL(connection);
        }
        ++iter;
    }
    m_closed_connection_set.swap(valid_connection_set);
}

void TcpReactor::destroy_normal_connections()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
    ConnectionSet::iterator iter = m_normal_connection_set.begin();
    while (m_normal_connection_set.end() != iter)
    {
        TcpConnection * connection = *iter;
        STUPID_DEL(connection);
        ++iter;
    }
    m_normal_connection_set.clear();
}

void TcpReactor::destroy_closed_connections()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
    ConnectionSet::iterator iter = m_closed_connection_set.begin();
    while (m_closed_connection_set.end() != iter)
    {
        TcpConnection * connection = *iter;
        STUPID_DEL(connection);
        ++iter;
    }
    m_closed_connection_set.clear();
}

void TcpReactor::destroy_binded_connections()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> binded_connection_set_guard(m_binded_connection_set_locker);
    m_binded_connection_set.clear();
}

void TcpReactor::destroy_connections()
{
    destroy_normal_connections();
    destroy_closed_connections();
    destroy_binded_connections();
}

void TcpReactor::append_data_events(DataEventsListVector & data_events_list_vector)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> data_events_guard(m_data_events_list_vector_locker);
    const size_t data_thread_count = m_data_events_list_vector.size();
    for (size_t thread_index = 0; thread_index < data_thread_count; ++thread_index)
    {
        DataEventsList & data_events_list = m_data_events_list_vector[thread_index];
        DataEventsList & events_list = data_events_list_vector[thread_index];
        data_events_list.splice(data_events_list.end(), events_list);
    }
}

void TcpReactor::delete_data_events(size_t thread_index, DataEventsList & events_list)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> data_events_guard(m_data_events_list_vector_locker);
    DataEventsList & data_events_list = m_data_events_list_vector[thread_index - 1];
    events_list.splice(events_list.end(), data_events_list);
}

void TcpReactor::clear_data_events()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> data_events_guard(m_data_events_list_vector_locker);
    m_data_events_list_vector.clear();
}

void TcpReactor::append_business_event(BusinessEvent & business_event)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> business_event_guard(m_business_event_list_vector_locker);
    const size_t business_thread_count = m_business_event_list_vector.size();
    TcpConnection * connection = business_event.connection;
    BusinessEventList & business_event_list = m_business_event_list_vector[connection->get_unique() % business_thread_count];
    business_event_list.push_back(business_event);
}

void TcpReactor::delete_business_event(size_t thread_index, BusinessEventList & event_list)
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> business_event_guard(m_business_event_list_vector_locker);
    BusinessEventList & business_event_list = m_business_event_list_vector[thread_index - 1];
    event_list.splice(event_list.end(), business_event_list);
}

void TcpReactor::clear_business_event()
{
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> business_event_guard(m_business_event_list_vector_locker);
    m_business_event_list_vector.clear();
}

void TcpReactor::reactor_connection_process()
{
    RUN_LOG_DBG("reactor connection thread begin");

    struct epoll_event connection_events[256];
    const size_t max_event_count = sizeof(connection_events) / sizeof(connection_events[0]);
    const size_t wait_timeout_ms = 1000;
    const size_t data_thread_count = m_data_events_list_vector.size();
    DataEventsListVector data_events_list_vector(data_thread_count);

    while (running())
    {
        int event_count = epoll_wait(m_epoll, connection_events, max_event_count, wait_timeout_ms);
        if (-1 == event_count)
        {
            if (-1 == m_epoll)
            {
                break;
            }

            if (stupid_is_net_blocking_error())
            {
                continue;
            }
            else
            {
                RUN_LOG_CRI("epoll wait failed: %d", stupid_net_error());
                break;
            }
        }
        else if (0 == event_count)
        {
            continue;
        }

        ConnectionVector listener_connections;

        for (int index = 0; index < event_count; ++index)
        {
            struct epoll_event & connection_event = connection_events[index];
            TcpConnection * connection = reinterpret_cast<TcpConnection *>(connection_event.data.ptr);
            if (m_listeners.end() != std::find(m_listeners.begin(), m_listeners.end(), connection))
            {
                listener_connections.push_back(connection);
            }
            else
            {
                Stupid::Base::Guard<Stupid::Base::ThreadLocker> connection_set_guard(m_connection_set_locker);
                if (m_normal_connection_set.end() != m_normal_connection_set.find(connection))
                {
                    DataEvents data_events;
                    data_events.connection = connection;
                    data_events.events = connection_event.events;

                    DataEventsList & data_events_list = data_events_list_vector[connection->get_unique() % data_thread_count];
                    data_events_list.push_back(data_events);
                    connection->increase_reference();
                }
            }
        }

        append_data_events(data_events_list_vector);

        ConnectionVector::iterator listener_iter = listener_connections.begin();
        while (listener_connections.end() != listener_iter)
        {
            do_accept(*listener_iter);
            ++listener_iter;
        }
        listener_connections.clear();
    }

    RUN_LOG_DBG("reactor connection thread end");
}

void TcpReactor::reactor_data_process(size_t thread_index)
{
    RUN_LOG_DBG("reactor data thread-%d begin", thread_index);

    DataEventsList data_events_list;

    while (running())
    {
        delete_data_events(thread_index, data_events_list);

        if (data_events_list.empty())
        {
            Stupid::Base::stupid_ms_sleep(10);
            continue;
        }

        DataEventsList::iterator iter = data_events_list.begin();
        while (data_events_list.end() != iter && running())
        {
            DataEvents & data_events = *iter;
            ++iter;

            TcpConnection * connection = data_events.connection;

            bool good = true;

            if (data_events.events & EPOLLIN)
            {
                if (!do_recv(connection))
                {
                    good = false;
                }
                if (connection->recv_buffer_size() >= connection->recv_buffer_water_mark())
                {
                    BusinessEvent business_event;
                    business_event.connection = data_events.connection;
                    business_event.event = recv_notify;
                    append_business_event(business_event);
                    connection->increase_reference();
                }
            }

            if (data_events.events & EPOLLOUT)
            {
                if (!do_send(connection))
                {
                    good = false;
                }
                else if (0 == connection->send_buffer_size())
                {
                    BusinessEvent business_event;
                    business_event.connection = data_events.connection;
                    business_event.event = send_notify;
                    append_business_event(business_event);
                    connection->increase_reference();

                    if (connection->get_eof())
                    {
                        good = false;
                    }
                    else if (!connection->get_error())
                    {
                        modify_connection_of_epoll(connection, false, true);
                    }
                }
            }

            if (!good || (data_events.events & EPOLLERR))
            {
                do_close(connection);
            }

            connection->decrease_reference();

            if (!connection->has_reference())
            {
                /* when get to here, maybe connection->has_reference() is true */
                remove_connection(connection);
            }
        }

        data_events_list.clear();
    }

    RUN_LOG_DBG("reactor data thread-%d end", thread_index);
}

void TcpReactor::reactor_business_process(size_t thread_index)
{
    RUN_LOG_DBG("reactor business thread-%d begin", thread_index);

    BusinessEventList business_event_list;

    while (running())
    {
        destroy_invalid_connections();

        delete_business_event(thread_index, business_event_list);

        if (business_event_list.empty())
        {
            Stupid::Base::stupid_ms_sleep(10);
            continue;
        }

        BusinessEventList::iterator iter = business_event_list.begin();
        while (business_event_list.end() != iter)
        {
            BusinessEvent & business_event = *iter;
            ++iter;

            bool good = false;

            TcpConnection * connection = business_event.connection;

            switch (business_event.event)
            {
                case connect_notify:
                {
                    good = handle_connect(connection, connection->get_identity());
                    break;
                }
                case accept_notify:
                {
                    good = handle_accept(connection, connection->get_listener_port());
                    break;
                }
                case recv_notify:
                {
                    good = handle_recv(connection);
                    break;
                }
                case send_notify:
                {
                    good = handle_send(connection);
                    break;
                }
                case close_notify:
                {
                    handle_close(connection);
                    good = true;
                    break;
                }
                default:
                {
                    good = false;
                    break;
                }
            }

            if (!good)
            {
                do_close(connection);
            }

            connection->decrease_reference();

            if (!connection->has_reference())
            {
                /* when get to here, maybe connection->has_reference() is true */
                remove_connection(connection);
            }
        }

        business_event_list.clear();
    }

    RUN_LOG_DBG("reactor business thread-%d end", thread_index);
}

NAMESPACE_STUPID_NET_END


#endif // _MSC_VER