// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2021 YADRO

#include <stdlib.h>

#include <boost/asio.hpp>
#include <libobmcsession/obmcsession.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>

#include <phosphor-logging/log.hpp>

#include <iostream>
#include <mutex>

using namespace boost::asio;
using namespace sdbusplus::message;
using namespace phosphor::logging;

using ConnectionPtr = std::shared_ptr<sdbusplus::asio::connection>;
using UnitInfo =
    std::tuple<std::string, std::string, std::string, std::string, std::string,
               std::string, object_path, uint32_t, std::string, object_path>;

using ListUnitInfo = std::vector<UnitInfo>;

static const auto sshUitsSessIdDict =
    std::make_unique<std::map<SessionIdentifier, std::string>>();

inline bool closeSSH(ConnectionPtr conn, const std::string& unitName);
inline void setupNewSessionSignals(ConnectionPtr conn);
static void asyncInitSessionList(ConnectionPtr conn, yield_context yield);

inline bool closeSSH(ConnectionPtr conn, const std::string& unitName)
{
    std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [sessionId, storedUnit] : *sshUitsSessIdDict)
    {
        if (!storedUnit.empty() && storedUnit == unitName)
        {
            sshUitsSessIdDict->erase(sessionId);
            break;
        }
    }
    try
    {
        auto callClose = conn->new_method_call(
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StopUnit");
        callClose.append(unitName, "ignore-dependencies");
        conn->call_noreply(callClose);
    }
    catch (std::exception& ex)
    {
        log<level::ERR>("Fail to send close SSH connection.",
                        entry("ERROR=%s", ex.what()));
        return false;
    }

    return true;
}

inline void setupNewSessionSignals(ConnectionPtr conn)
{
    using namespace sdbusplus::bus::match;

    static const auto newSshRule =
        rules::type::signal() + rules::path("/org/freedesktop/systemd1") +
        rules::interface("org.freedesktop.systemd1.Manager") +
        rules::member("UnitNew");

    static const auto delSshRule =
        rules::type::signal() + rules::path("/org/freedesktop/systemd1") +
        rules::interface("org.freedesktop.systemd1.Manager") +
        rules::member("UnitRemoved");

    static match newSSHWatcher(
        *conn, newSshRule, [conn](sdbusplus::message::message& message) {
            std::string unitName;
            try
            {
                message.read(unitName);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Failed to read message of UnitNew signal, PATH="
                          << message.get_path() << ": " << e.what();
            }

            if (unitName.starts_with("dropbear@"))
            {
                std::cout << "New SSH connection: " << unitName << std::endl;
                obmcsesSessionId sessionId;
                try
                {
                    auto r = obmcsesCreateTransactionWithFCleanup(
                        [conn](obmcsesSessionId id) -> OBMCBool {
                            auto sshUnitIt = sshUitsSessIdDict->find(id);
                            if (sshUnitIt == sshUitsSessIdDict->end())
                            {
                                return false;
                            }
                            return closeSSH(conn, sshUnitIt->second);
                        },
                        &sessionId);
                    if (r)
                    {
                        std::cerr << "Fail to create obmcsess object: "
                                  << std::strerror(r) << std::endl;
                        return;
                    }
                    sshUitsSessIdDict->emplace(sessionId, unitName);
                }
                catch (std::exception& ex)
                {
                    std::cerr << "Failed: " << ex.what() << std::endl;
                }
            }
        });

    static match delSSHWatcher(
        *conn, delSshRule, [](sdbusplus::message::message& message) {
            std::string unitName;
            try
            {
                message.read(unitName);
            }
            catch (const std::exception& e)
            {
                std::cerr
                    << "Failed to read message of UnitRemoved signal, PATH="
                    << message.get_path() << ": " << e.what();
            }

            if (unitName.starts_with("dropbear@"))
            {
                std::cout << "The SSH connection closed: " << unitName
                          << std::endl;
                for (const auto& [sessionId, storedUnit] : *sshUitsSessIdDict)
                {
                    if (unitName == storedUnit)
                    {
                        if (!obmcsesRemoveWithoutCleanup(sessionId))
                        {
                            std::cerr
                                << "Failed to close ssh session:" << sessionId
                                << std::endl;
                        }
                    }
                }
            }
        });
}

static void asyncInitSessionList(ConnectionPtr conn, yield_context yield)
{
    boost::system::error_code ec;
    const auto listUnits = conn->yield_method_call<ListUnitInfo>(
        yield, ec, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "ListUnits");

    if (ec)
    {
        std::cerr << "Fail to call the ListUnit dbus method: " << ec.message()
                  << std::endl;
        return;
    }
    for (const UnitInfo& unitInfo : listUnits)
    {
        try
        {
            auto& unitName = std::get<0>(unitInfo);
            if (unitName.starts_with("dropbear@"))
            {
                std::cout << "Found SSH session: " << unitName << std::endl;
                closeSSH(conn, unitName);
            }
        }
        catch (std::out_of_range& ex)
        {
            std::cerr << "Fail to retrieve the UnitName field from UnitInfo: "
                      << ex.what() << std::endl;
        }
    }
}

int main()
{
    io_context io;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io);

    int r = obmcsesManagerInitAsio(*conn, "SSH", obmcsessTypeManagerConsole);

    if (r != 0)
    {
        return r;
    }
    setupNewSessionSignals(conn);

    spawn(io, [conn](boost::asio::yield_context yield) {
        asyncInitSessionList(conn, yield);
    });

    io.run();

    return EXIT_SUCCESS;
}