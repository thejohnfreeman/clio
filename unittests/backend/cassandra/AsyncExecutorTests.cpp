//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <backend/cassandra/impl/FakesAndMocks.h>
#include <util/Fixtures.h>

#include <backend/cassandra/Error.h>
#include <backend/cassandra/impl/AsyncExecutor.h>

#include <gmock/gmock.h>

using namespace Backend::Cassandra;
using namespace Backend::Cassandra::detail;
using namespace testing;

class BackendCassandraAsyncExecutorTest : public SyncAsioContextTest
{
};

TEST_F(BackendCassandraAsyncExecutorTest, CompletionCalledOnSuccess)
{
    auto statement = FakeStatement{};
    auto handle = MockHandle{};

    ON_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .WillByDefault([this](auto const&, auto&& cb) {
            ctx.post([cb = std::move(cb)]() { cb({}); });
            return FakeFutureWithCallback{};
        });
    EXPECT_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .Times(AtLeast(1));

    auto called = std::atomic_bool{false};
    auto work = std::optional<boost::asio::io_context::work>{ctx};

    AsyncExecutor<FakeStatement, MockHandle>::run(ctx, handle, std::move(statement), [&called, &work](auto&&) {
        called = true;
        work.reset();
    });

    ctx.run();
    ASSERT_TRUE(called);
}

TEST_F(BackendCassandraAsyncExecutorTest, ExecutedMultipleTimesByRetryPolicyOnMainThread)
{
    auto callCount = std::atomic_int{0};
    auto statement = FakeStatement{};
    auto handle = MockHandle{};

    // emulate successfull execution after some attempts
    ON_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .WillByDefault([&callCount](auto const&, auto&& cb) {
            ++callCount;
            if (callCount >= 3)
                cb({});
            else
                cb({CassandraError{"timeout", CASS_ERROR_LIB_REQUEST_TIMED_OUT}});

            return FakeFutureWithCallback{};
        });
    EXPECT_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .Times(3);

    auto called = std::atomic_bool{false};
    auto work = std::optional<boost::asio::io_context::work>{ctx};

    AsyncExecutor<FakeStatement, MockHandle>::run(ctx, handle, std::move(statement), [&called, &work](auto&&) {
        called = true;
        work.reset();
    });

    ctx.run();
    ASSERT_TRUE(callCount >= 3);
    ASSERT_TRUE(called);
}

TEST_F(BackendCassandraAsyncExecutorTest, ExecutedMultipleTimesByRetryPolicyOnOtherThread)
{
    auto callCount = std::atomic_int{0};
    auto statement = FakeStatement{};
    auto handle = MockHandle{};

    auto threadedCtx = boost::asio::io_context{};
    auto work = std::optional<boost::asio::io_context::work>{threadedCtx};
    auto thread = std::thread{[&threadedCtx] { threadedCtx.run(); }};

    // emulate successfull execution after some attempts
    ON_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .WillByDefault([&callCount](auto const&, auto&& cb) {
            ++callCount;
            if (callCount >= 3)
                cb({});
            else
                cb({CassandraError{"timeout", CASS_ERROR_LIB_REQUEST_TIMED_OUT}});

            return FakeFutureWithCallback{};
        });
    EXPECT_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .Times(3);

    auto called = std::atomic_bool{false};
    auto work2 = std::optional<boost::asio::io_context::work>{ctx};

    AsyncExecutor<FakeStatement, MockHandle>::run(
        threadedCtx, handle, std::move(statement), [&called, &work, &work2](auto&&) {
            called = true;
            work.reset();
            work2.reset();
        });

    ctx.run();
    ASSERT_TRUE(callCount >= 3);
    ASSERT_TRUE(called);
    threadedCtx.stop();
    thread.join();
}

TEST_F(BackendCassandraAsyncExecutorTest, CompletionCalledOnFailureAfterRetryCountExceeded)
{
    auto statement = FakeStatement{};
    auto handle = MockHandle{};

    // FakeRetryPolicy returns false for shouldRetry in which case we should
    // still call onComplete giving it whatever error we have raised internally.
    ON_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .WillByDefault([](auto const&, auto&& cb) {
            cb({CassandraError{"not a timeout", CASS_ERROR_LIB_INTERNAL_ERROR}});
            return FakeFutureWithCallback{};
        });
    EXPECT_CALL(handle, asyncExecute(An<FakeStatement const&>(), An<std::function<void(FakeResultOrError)>&&>()))
        .Times(1);

    auto called = std::atomic_bool{false};
    auto work = std::optional<boost::asio::io_context::work>{ctx};

    AsyncExecutor<FakeStatement, MockHandle, FakeRetryPolicy>::run(
        ctx, handle, std::move(statement), [&called, &work](auto&& res) {
            EXPECT_FALSE(res);
            EXPECT_EQ(res.error().code(), CASS_ERROR_LIB_INTERNAL_ERROR);
            EXPECT_EQ(res.error().message(), "not a timeout");

            called = true;
            work.reset();
        });

    ctx.run();
    ASSERT_TRUE(called);
}
