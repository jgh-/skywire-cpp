#include <skywire/event_loop.hh>
#include <skywire/log.hh>

#include <sys/signalfd.h>
#include <signal.h>
#include <assert.h>

namespace skywire {
    
    event_loop::event_loop(int max_events, bool handle_signals) 
    : fd_wakeup_(-1)
    , fd_signal_(-1)
    , fd_epoll_(-1)
    , max_events_(max_events)
    , exiting_(0) 
    {
        struct epoll_event event = {0};

        fd_epoll_ = epoll_create1(0);
        
        if(fd_epoll_ < 0) {
            log().critical("epoll: {}", strerror(errno));
            exit(errno);
        }

        fd_wakeup_ = eventfd(0, EFD_NONBLOCK);

        if(fd_wakeup_ < 0) {
            log().critical("eventfd: {}", strerror(errno));
            exit(errno);
        }

        event.data.fd = fd_wakeup_;
        event.events = EPOLLIN | EPOLLET;
        epoll_ctl(fd_epoll_, EPOLL_CTL_ADD, fd_wakeup_, &event);


        if(handle_signals) {
            sigset_t mask;
            sigemptyset(&mask);
            sigaddset(&mask, SIGINT); // graceful shutdown
            //sigaddset(&mask, SIGTERM); // immediate shutdown
            int err = sigprocmask(SIG_BLOCK, &mask, NULL);
            if(err) {
                log().critical("sigprocmask: {}", strerror(errno));
                exit(errno);
            }
            fd_signal_ = signalfd(-1, &mask, 0);
            if(fd_signal_ == -1) {
                log().critical("signalfd: {}", strerror(errno));
                exit(errno);   
            }
            event.data.fd = fd_signal_;
            event.events = EPOLLIN | EPOLLET;
            epoll_ctl(fd_epoll_, EPOLL_CTL_ADD, fd_signal_, &event);
        }
    }

    event_loop::~event_loop() 
    {
        if(fd_signal_ > -1) {
            close(fd_signal_);
        }
        close(fd_epoll_);
        close(fd_wakeup_);
    }

    void
    event_loop::register_handler(int fd, event_handler_f handler, int events)
    {
        if(fd_epoll_ > -1 && fd > -1) {
            handlers_[fd] = handler;
            struct epoll_event event = {0};
            event.data.fd = fd;
            event.events = events;
            epoll_ctl(fd_epoll_, EPOLL_CTL_ADD, fd, &event);
        }
        if(fd_wakeup_)
        {
            eventfd_write(fd_wakeup_, 1);
        }
    }

    void
    event_loop::unregister_handler(int fd)
    {
        if(fd_epoll_ > -1) {
            handlers_.erase(fd);
            epoll_ctl(fd_epoll_, EPOLL_CTL_DEL, fd, nullptr);
        }
        if(fd_wakeup_)
        {
            eventfd_write(fd_wakeup_, 1);
        }
    }

    void
    event_loop::run()
    {
        exiting_ = false;

        struct epoll_event* events = nullptr;

        events = (struct epoll_event*)calloc(max_events_, sizeof(struct epoll_event));
        if(!events) {
            log().critical("Couldn't allocate events!");
            exit(-1);
        }
        int exiting = 0;
        while((exiting = exiting_.load()) != 1) {
            int res = epoll_wait(fd_epoll_, events, max_events_, -1);
            exiting = exiting_.load();
            if(res > 0) {
                for( int i = 0 ; i < res && exiting != 1 ; ++i ) {
                    if(events[i].data.fd == fd_wakeup_) {
                        // Wakeup
                        eventfd_t val;
                        eventfd_read(fd_wakeup_, &val);
                    }
                    else if(events[i].data.fd == fd_signal_) {
                        // Received signal??
                        struct signalfd_siginfo si;
                        read(fd_signal_, &si, sizeof(si));
                        if(si.ssi_signo == SIGINT) {
                            log().info("Got SIGINT, shutting down gracefully.");
                            exiting = 2;
                            if(shutdown_handler_) {
                                shutdown_handler_(*this);
                            }
                        } else if(si.ssi_signo == SIGTERM) {
                            log().info("Got SIGTERM, shutting down immediately.");
                            exiting_ = 1;
                            exiting = 1;
                        }
                    }
                    else {
                        auto it = handlers_.find(events[i].data.fd);
                        if(it != handlers_.end()) {
                            // found the handler, let's do it.
                            (*it).second(events[i].data.fd, events[i].events);
                        }
                    }
                }    
            }
            if(handlers_.size() == 0 && exiting == 2) {
                exiting_ = 1;
            }
        }
     
        free(events);

        if(shutdown_handler_) {
            shutdown_handler_(*this);
        }
    }

    void
    event_loop::stop(bool graceful)
    { 
        exiting_ = 1 + graceful;
        if(fd_wakeup_)
        {
            eventfd_write(fd_wakeup_, 1);
        }
    }
}