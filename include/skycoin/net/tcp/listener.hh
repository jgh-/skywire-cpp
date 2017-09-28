#ifndef SKYCOIN__TCP_LISTENER_HH
#define SKYCOIN__TCP_LISTENER_HH

#include <skycoin/net/tcp/connection.hh>
#include <skycoin/net/listener.hh>

#include <unpause/async>
#include <memory>
#include <list>

namespace skycoin { namespace tcp {

    class listener : public i_listener {
    public:
        
        listener(std::string addr, int port, unpause::async::thread_pool& pool);
        ~listener();
        
        event_handler_f handler() { return [this](int fd, uint32_t events) { return handle_events(events); }; }

        virtual int fd() const { return fd_; }
        virtual i_connection* connection_for_fd(int fd) {
            for(auto& it : connections_) {

            }
            return nullptr;
        };

    protected:
        virtual int handle_events(uint32_t events);
        virtual void handle_new_connection(int fd);

    protected:
        std::list<std::unique_ptr<connection>> connections_;
        std::string addr_;
        unpause::async::thread_pool& pool_;

        int fd_;
        int port_;

    };

} // namespace tcp
} // namespace skycoin

#endif // skycoin__TCP_LISTENER_HH