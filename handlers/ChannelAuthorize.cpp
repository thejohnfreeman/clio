//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/basics/StringUtilities.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/PayChan.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <handlers/RPCHelpers.h>
#include <optional>

void
serializePayChanAuthorization(
    ripple::Serializer& msg,
    ripple::uint256 const& key,
    ripple::XRPAmount const& amt)
{
    msg.add32(ripple::HashPrefix::paymentChannelClaim);
    msg.addBitString(key);
    msg.add64(amt.drops());
}

boost::json::object
doChannelAuthorize(boost::json::object const& request)
{
    boost::json::object response;
    if(!request.contains("channel_id"))
    {
        response["error"] = "missing field channel_id";
        return response;
    }

    if(!request.contains("amount"))
    {
        response["error"] = "missing field amount";
        return response;
    }

    if (!request.contains("key_type") && !request.contains("secret"))
    {
        response["error"] = "missing field secret";
        return response;
    }

    boost::json::value error = nullptr;
    auto const [pk, sk] = keypairFromRequst(request, error);
    if (!error.is_null())
    {
        response["error"] = error;
        return response;
    }

    ripple::uint256 channelId;
    if (!channelId.parseHex(request.at("channel_id").as_string().c_str()))
    {
        response["error"] = "channel id malformed";
        return response;
    }

    if (!request.at("amount").is_string())
    {
        response["error"] = "channel amount malformed";
        return response;
    }

    auto optDrops =
        ripple::to_uint64(request.at("amount").as_string().c_str());

    if (!optDrops)
    {
        response["error"] = "could not parse channel amount";
        return response;
    }

    std::uint64_t drops = *optDrops;

    ripple::Serializer msg;
    ripple::serializePayChanAuthorization(msg, channelId, ripple::XRPAmount(drops));

    try
    {
        auto const buf = ripple::sign(pk, sk, msg.slice());
        response["signature"] = ripple::strHex(buf);
    }
    catch (std::exception&)
    {
        response["error"] = "Exception occurred during signing.";
        return response;
    }

    return response;
}   