# 🔔 Notification Server

A production-ready, horizontally scalable real-time notification server written in **C++17**.

```
Client (WebSocket)
       ↓
C++ Notification Server  ←→  Redis Pub/Sub  ←→  More Server Instances
```

## Features

- **WebSocket** connections for real-time push delivery
- **Redis Pub/Sub** for horizontal scaling (multiple server instances)
- **Idempotency** — duplicate notifications are automatically deduplicated
- **Metrics endpoint** — active connections, messages sent, failures
- **CORS-ready** — works with any frontend
- **Zero-config** — runs standalone without Redis (single-instance mode)
- **Docker + docker-compose** — one command to run everything

---

## API Reference

### `GET /health`
```json
{ "status": "ok", "service": "notification-server" }
```

### `POST /notify`
Send a notification to a user.

**Body:**
```json
{
  "user_id": "alice",
  "message": "Your order has shipped!",
  "type": "order"
}
```

**Response:**
```json
{ "status": "queued", "ok": true }
```

### `GET /metrics`
```json
{
  "active_connections": 5,
  "messages_sent": 120,
  "failed_deliveries": 0,
  "redis_published": 120,
  "redis_received": 120,
  "uptime_seconds": 3600
}
```

### `GET /connections`
```json
{
  "count": 2,
  "users": ["alice", "user-123"]
}
```

### `WebSocket ws://host/ws?user_id=YOUR_ID`
Connect via WebSocket. Pass your `user_id` as a query param.

**Server sends on connect:**
```json
{ "type": "connected", "user_id": "alice", "message": "Connection established" }
```

**Heartbeat:**
Send `{"type":"ping"}` → server replies `{"type":"pong"}`

**Notification delivery:**
```json
{
  "type": "notification",
  "id": "a3f2b1...",
  "message": "Your order has shipped!",
  "notif_type": "order",
  "timestamp": 1710000000000
}
```

---

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `PORT` | `8080` | HTTP/WebSocket port |
| `REDIS_URL` | `tcp://127.0.0.1:6379` | Redis connection string |
| `NOTIF_CHANNEL` | `notifications` | Redis Pub/Sub channel |
| `CONCURRENCY` | `4` | Thread pool size |

---

## Local Development

### Prerequisites

- C++17 compiler (`g++` / `clang++`)
- CMake ≥ 3.16
- Redis (optional — server degrades gracefully without it)
- libboost-dev, libssl-dev, libhiredis-dev

**Ubuntu/Debian:**
```bash
sudo apt install build-essential cmake libboost-all-dev libssl-dev libhiredis-dev redis
```

**macOS:**
```bash
brew install cmake boost openssl hiredis redis
```

### Build

```bash
git clone <your-repo-url>
cd notification-server

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

./build/notification-server
```

### Run tests

```bash
chmod +x test.sh
./test.sh
```

### WebSocket test (requires `wscat`)

```bash
npm install -g wscat
wscat -c "ws://localhost:8080/ws?user_id=alice"
```

In another terminal:
```bash
curl -X POST http://localhost:8080/notify \
  -H "Content-Type: application/json" \
  -d '{"user_id":"alice","message":"Hello!","type":"info"}'
```

You'll see the notification appear in the `wscat` terminal instantly.

---

## Docker (Local)

Run the full stack (2 server instances + Redis) with one command:

```bash
docker-compose up --build
```

This starts:
- `redis` on port `6379`
- `server1` on port `8080`
- `server2` on port `8081`

Both servers share the same Redis, so a notification sent to either port is delivered regardless of which instance the user is connected to.

---



## Scaling Horizontally

To run 3 instances in Docker:

```bash
docker-compose up --scale server1=3
```

Add a load balancer (like nginx) in front:

```nginx
upstream notification_servers {
    server server1:8080;
    server server2:8080;
    server server3:8080;
}

server {
    listen 80;
    location / {
        proxy_pass http://notification_servers;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";  # required for WebSockets
    }
}
```

Because all instances subscribe to the same Redis channel, any notification published to any instance is delivered to the correct user regardless of which server they're connected to.

---

## Architecture

```
Client connects via WebSocket
         ↓
Server registers user_id → connection in ConnectionManager
         ↓
POST /notify arrives at any server instance
         ↓
Server publishes JSON to Redis channel "notifications"
         ↓
ALL server instances receive the message via Redis Pub/Sub
         ↓
Each instance checks: "is this user connected HERE?"
         ↓
The correct instance sends the message via WebSocket
```

**Deduplication:** Each notification gets a unique ID. Redis stores processed IDs for 24h. Duplicate deliveries are silently dropped.

---

## Project Structure

```
notification-server/
├── src/
│   ├── main.cpp                # Server setup, routes, WebSocket handler
│   ├── ConnectionManager.h/.cpp  # user_id → WebSocket connection map
│   ├── NotificationService.h/.cpp  # publish/deliver logic + dedup
│   ├── RedisSubscriber.h/.cpp  # Background Redis SUBSCRIBE thread
│   └── MetricsCollector.h/.cpp # Metrics tracking
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
├── fly.toml                    # Fly.io deployment config
└── test.sh                     # Quick test script
```
