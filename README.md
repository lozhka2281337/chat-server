[![License](https://img.shields.io/badge/License-MIT-yellow.svg)]()
[![Language](https://img.shields.io/badge/language-C-blue.svg)]()
[![Platform](https://img.shields.io/badge/platform-Linux-green.svg)]()
[![Timeout](https://img.shields.io/badge/timeout-30s-orange.svg)]()

### education TCP Chat Server with support for 100+ concurrent clients, non-blocking I/O, activity timeouts, and automatic disconnection of inactive clients

### demonstration
`[28-12-2025 17:25:43][INFO] server is waiting connection with timeout 30 s...`
`[28-12-2025 17:26:01][INFO] new connection from 127.0.0.1: number 1; socket: 4`
`[28-12-2025 17:26:02][INFO] new connection from 127.0.0.1: number 2; socket: 5`
`[28-12-2025 17:26:10][INFO] bye-bye, socket 4`
`[28-12-2025 17:26:15][ERROR] failed to send full message: 512/1024 bytes`

### compilation and start
`make` (or `gcc server.c -o chat_server`)
`./chat_server`

### configuration
`./chat_server -timeout 10`

### test
`for i in {1..100}; do echo "test $i" | nc localhost 8888 & done`


