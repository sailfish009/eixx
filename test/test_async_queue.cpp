//----------------------------------------------------------------------------
/// \file  test_mailbox.cpp
//----------------------------------------------------------------------------
/// \brief Test cases for basic_otp_mailbox.
//----------------------------------------------------------------------------
// Copyright (c) 2010 Serge Aleynikov <saleyn@gmail.com>
// Created: 2010-09-23
//----------------------------------------------------------------------------
/*
***** BEGIN LICENSE BLOCK *****

This file is part of the EPI (Erlang Plus Interface) Library.

Copyright (C) 2010 Serge Aleynikov <saleyn@gmail.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

***** END LICENSE BLOCK *****
*/

#define BOOST_TEST_MODULE test_async_queue

//#define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <atomic>
#include <thread>
#include <boost/thread.hpp>
#include <boost/test/unit_test.hpp>
#include <eixx/util/async_queue.hpp>
#include <eixx/connect/test_helper.hpp>

using namespace eixx::util;

int b, n, i;

BOOST_AUTO_TEST_CASE( test_async_queue )
{
    boost::asio::io_service io;

    std::shared_ptr<async_queue<int>> q(new async_queue<int>(io));

    bool res = q->async_dequeue(
        [](int& a, const boost::system::error_code& ec) { i = a; return false; },
        std::chrono::milliseconds(0));

    BOOST_REQUIRE(!res);

    for (i = 10; i < 13; i++)
        BOOST_REQUIRE(q->enqueue(i));

    for (int j = 10; j < 13; j++) {
        BOOST_REQUIRE(q->async_dequeue(
            [j](int& a, const boost::system::error_code& ec) {
                BOOST_REQUIRE_EQUAL(j, a);
                return true;
            },
            std::chrono::milliseconds(0)));
    }

    b = 15;
    bool r = q->async_dequeue(
        [](int& a, const boost::system::error_code& ec) {
            BOOST_REQUIRE_EQUAL(b++, a);
            n++;
            return true;
        }, 3);
    BOOST_REQUIRE(!r);

    i = 15;

    BOOST_REQUIRE(q->enqueue(i++));
    BOOST_REQUIRE(q->enqueue(i++));
    BOOST_REQUIRE(q->enqueue(i++));

    io.run();

    BOOST_REQUIRE_EQUAL(i, b);
    BOOST_REQUIRE_EQUAL(3, n);
}

namespace {
    const int iterations = getenv("ITERATIONS") ? atoi("ITERATIONS") : 1000000;

    std::atomic_int producer_count(0);
    std::atomic_int consumer_count(0);

    const int producer_thread_count = 4;
    std::atomic_int done_producer_count(0);

    int total_iterations = producer_thread_count * iterations;

    std::atomic<bool> done (false);
}

void producer(async_queue<int>& q, std::atomic_int& n, int i)
{
    for (int i = 0; i != iterations; ++i) {
        int value = ++n;
        while (!q.enqueue(value));
    }

    if (eixx::verboseness::level() >= eixx::connect::VERBOSE_DEBUG)
        std::cout << "Producer thread " << i << " done" << std::endl;

    done = ++done_producer_count == producer_thread_count;
}

BOOST_AUTO_TEST_CASE( test_async_queue_concurrent )
{
    boost::asio::io_service io;

    std::shared_ptr<async_queue<int>> q(new async_queue<int>(io, 128));

    boost::thread_group producer_threads;

    for (int i = 0; i < producer_thread_count; ++i)
        producer_threads.create_thread(
            [&q, i] () { producer(*q, producer_count, i+1); }
        );

    while (q->async_dequeue(
        [] (int& v, const boost::system::error_code& ec) {
            consumer_count++;
            return consumer_count < total_iterations;
        },
        std::chrono::milliseconds(1000),
        true));

    io.run();
    io.reset();

    producer_threads.join_all();

    if (eixx::verboseness::level() >= eixx::connect::VERBOSE_DEBUG) {
        std::cout << "Produced " << producer_count << " objects." << std::endl;
        std::cout << "Consumed " << consumer_count << " objects." << std::endl;
    }

    auto r = q->async_dequeue(
        [] (int& v, const boost::system::error_code& ec) { return true; },
        std::chrono::milliseconds(8000),
        -1);

    BOOST_REQUIRE(!r);

    boost::asio::system_timer t(io);
    t.expires_from_now(std::chrono::milliseconds(1));
    t.async_wait([&q](const boost::system::error_code& e) {
        q->cancel();
        if (eixx::verboseness::level() >= eixx::connect::VERBOSE_DEBUG)
            std::cout << "Canceled timer" << std::endl;
    });

    io.run();

    if (eixx::verboseness::level() >= eixx::connect::VERBOSE_DEBUG)
        std::cout << "Done!" << std::endl;
}

