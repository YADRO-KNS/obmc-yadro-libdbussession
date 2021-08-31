# OpenBMC session manager library

The `libobmcsession` library aims to provide a common way to manage OpenBMC user sessions of any subsystem in which an authorization feature is used.

## Build with OpenBMC SDK
OpenBMC SDK contains toolchain and all dependencies needed for building the
project. See [official
documentation](https://github.com/openbmc/docs/blob/master/development/dev-environment.md#download-and-install-sdk)
for details.

Build steps:
```sh
$ source /path/to/sdk/environment-setup-arm1176jzs-openbmc-linux-gnueabi
$ meson --buildtype plain --optimization s build_dir
$ ninja -C build_dir
```
If build process succeeded, the directory `build_dir` contains executable file
`libobmcsession.so`.

## Design

The `libobmcsession` provide public interface to create/clear session with publishing on the DBus.

```
+----------------------------------------------------------------+--------------------------------------------------------------+
|                                                                |                                                              |
|                      Libobmcsession                            |                          Service 1                           |
|                                                                |                                                              |
|                                                                |        +---------------------+                               |
|                                                                |        |    authorization    +----+                          |
|                                                                |        +---------------------+    |                          |
|                                                                |                                   |                          |
| +-----------------------------------------+                    |                                 +-+------------------------+ |
| |Publish SessionManager object on the DBus|<----------+        |                                 | internal session stuff   | |
| +-----------------------------------------+           |        |                                 +-+------------------------+ |
|                                                       |        |                                   |                          |
|       +-----------------------+                       |        |        +--------------------------v------------+             |
|       | Create session object |<------------+         +--------+--------+ create libobmcsession::SessionManager |             |
|       +----------+------------+             |                  |        +---------------------------------------+             |
|                  |                          |                  |                                                              |
| +----------------v-------------------+      |                  |                                                              |
| | Publish Session object on the DBus |      |                  |                                                              |
| +------------------------------------+      |                  |                                                              |
|                                             |                  |                                                              |
|        +----------------------+             |                  |         +--------------------------------------------+       |
|        |Adjust session cleanup|<------------+------------------+---------+call libobmcsession::SessionManager::create |       |
|        +-------^--------------+                                |         +--------------------------------------------+       |
|                |                                               |                                                              |
|                |                                               |                                                              |
|                |                                               |                            ...                               |
|                |                                               |                                                              |
|                |                                               |                                                              |
|                |  +-----------------------+                    |         +--------------------------------------------+       |
|                +--+Remove object from DBus+-----------+--------+---------+call libobmcsession::SessionManager::remove |       |
|                   +-----------^-----------+           |        |         +--------------------------------------------+       |
|                               |                       |        |                                                              |
|                               |                       |        |                                                              |
|                               |                       |        |                                                              |
|                               |                       |        |                                                              |
|                               |          +------------+        |                                                              |
|                               |          |                     +--------------------------------------------------------------+
|                               |          |                     |                                                              |
|                               |          v                     |                           Service 2                          |
|              +----------------+----------+------+              |                                                              |
|              |Call DBus method                  |              |                                                              |
|              |xyz.openbmc_project.Object.Delete |              |                                                              |
|              ++---------------+----------------++              |                                                              |
|                               ^                                |     +-------------------------------------------------+      |
|               +---------------|----------------+               |     |  Remove service_1 session by condition          |      |
|               |Call SessionManager::remove(...)|<--------------+-----+(username, remoteAddress, SessionId, sessionType)|      |
|               +--------------------------------+               |     +-------------------------------------------------+      |
|                                                                |                                                              |
|                                                                |                                                              |
+----------------------------------------------------------------+--------------------------------------------------------------+
```
