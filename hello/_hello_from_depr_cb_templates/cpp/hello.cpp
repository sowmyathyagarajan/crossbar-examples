///////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) Crossbar.io Technologies GmbH and/or collaborators. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include <autobahn/autobahn.hpp>
#include <boost/asio.hpp>
#include <boost/version.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <tuple>

using boost::asio::ip::tcp;
using boost::asio::ip::address;

void add2(autobahn::wamp_invocation invocation)
{
    auto a = invocation->argument<uint64_t>(0);
    auto b = invocation->argument<uint64_t>(1);

    std::cerr << "procedure com.example.add2 invoked: " << a << ", " << b << std::endl;

    invocation->result(std::make_tuple(a + b));
}

void on_topic1(const autobahn::wamp_event& event)
{
    std::cerr << "received event: " << event.argument<std::string>(0) << std::endl;
}


int main(int argc, char** argv)
{
    const std::string realm = "realm1";
    const tcp::endpoint endpoint = tcp::endpoint(address::from_string("127.0.0.1"), 8000);

    std::cerr << "Boost: " << BOOST_VERSION << std::endl;

    try {
        std::cerr << "Connecting to realm: " << realm << std::endl;

        boost::asio::io_service io;
        tcp::socket socket(io);

        // create a WAMP session that talks WAMP-RawSocket over TCP
        //
        bool debug = false;
        auto session = std::make_shared<
                autobahn::wamp_session<tcp::socket, tcp::socket>>(io, socket, socket, debug);

        // Make sure the continuation futures we use do not run out of scope prematurely.
        // Since we are only using one thread here this can cause the io service to block
        // as a future generated by a continuation will block waiting for its promise to be
        // fulfilled when it goes out of scope. This would prevent the session from receiving
        // responses from the router.
        boost::future<void> start_future;
        boost::future<void> join_future;

        // same for other vars we need to keep alive ..
        //
        int counter = 0;
        boost::asio::deadline_timer timer(io, boost::posix_time::seconds(1));
        std::function<void ()> loop;
        boost::future<void> c1;

        socket.async_connect(endpoint,

            // we either connected or an error happened during connect ..
            //
            [&](boost::system::error_code ec) {

                if (!ec) {
                    std::cerr << "connected to server" << std::endl;

                    // start the WAMP session on the transport that has been connected
                    //
                    start_future = session->start().then([&](boost::future<bool> started) {
                        if (started.get()) {
                            std::cerr << "session started" << std::endl;

                            // join a realm with the WAMP session
                            //
                            join_future = session->join(realm).then([&](boost::future<uint64_t> s) {
                                std::cerr << "joined realm: " << s.get() << std::endl;
 
                                // SUBSCRIBE to a topic and receive events
                                //
                                session->subscribe("com.example.onhello",
                                    [](const autobahn::wamp_event& event) {
                                       std::cerr << "event for 'onhello' received: " << event.argument<std::string>(0) << std::endl;
                                    }
                                );

                                // REGISTER a procedure for remote calling
                                //
                                session->provide("com.example.add2", &add2);

                                // PUBLISH and CALL every second .. forever
                                //
                                loop = [&]() {
                                    timer.async_wait([&](boost::system::error_code) {

                                        // PUBLISH an event
                                        //
                                        session->publish("com.example.oncounter", std::tuple<int>(counter));
                                        std::cerr << "published to 'oncounter' with counter " << counter << std::endl;
                                        counter += 1;
  
                                        // CALL a remote procedure
                                        //
                                        c1 = session->call("com.example.mul2", std::tuple<int, int>(counter, 3))
                                            .then([&](boost::future<autobahn::wamp_call_result> result) {
                                                try {
                                                    uint64_t product = result.get().argument<uint64_t>(0);
                                                    std::cerr << "call succeeded with result: " << product << std::endl;
                                                } catch (const std::exception& e) {
                                                    std::cerr << "call failed: " << e.what() << std::endl;
                                                }
                                            }
                                        );

                                        timer.expires_at(timer.expires_at() + boost::posix_time::seconds(1));
                                        loop();
                                    });
                                };
  
                                loop();
                            });
                        } else {
                            std::cerr << "failed to start session" << std::endl;
                            io.stop();
                        }
                    });
                } else {
                    std::cerr << "connect failed: " << ec.message() << std::endl;
                }
            }
        );

        std::cerr << "starting io service" << std::endl;
        io.run();
        std::cerr << "stopped io service" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
