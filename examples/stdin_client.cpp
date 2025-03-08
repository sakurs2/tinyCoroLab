#include "coro/coro.hpp"

using namespace coro;

#define BUFFLEN 10240

task<> echo(int sockfd)
{
    char buf[BUFFLEN] = {0};
    int  ret          = 0;
    auto conn         = net::tcp_connector(sockfd);

    while (true)
    {
        ret = co_await net::stdin_awaiter(buf, BUFFLEN, 0);
        log::info("receive data from stdin: {}", buf);
        ret = co_await conn.write(buf, ret);
    }
}

task<> client(const char* addr, int port)
{
    auto client = net::tcp_client(addr, port);
    int  ret    = 0;
    int  sockfd = 0;
    sockfd      = co_await client.connect();
    assert(sockfd > 0 && "connect error");

    submit_to_scheduler(echo(sockfd));

    char buf[BUFFLEN] = {0};
    auto conn         = net::tcp_connector(sockfd);
    while ((ret = co_await conn.read(buf, BUFFLEN)) > 0)
    {
        log::info("receive data from net: {}", buf);
    }

    ret = co_await conn.close();
    assert(ret == 0);
}

int main(int argc, char const* argv[])
{
    /* code */
    scheduler::init();
    submit_to_scheduler(client("localhost", 8000));

    scheduler::start();
    scheduler::join();
    return 0;
}
