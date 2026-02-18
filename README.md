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

## 🚀 Free Deployment Options

### Option 1: Fly.io ⭐ (Recommended — Best for WebSockets)

Fly.io gives you **free VMs with WebSocket support**. It handles persistent connections properly, unlike serverless platforms.

**Step 1: Install Fly CLI**
```bash
curl -L https://fly.io/install.sh | sh
```

**Step 2: Sign up and log in**
```bash
fly auth signup   # or: fly auth login
```

**Step 3: Create a free Redis instance**
```bash
fly redis create
# Choose: free tier, no eviction
# Copy the redis URL shown — looks like: redis://default:password@...fly.io:6379
```

**Step 4: Deploy the server**
```bash
fly launch
# When asked: don't add a Postgres database, don't add a Redis database (you made one above)
# Edit fly.toml: set app = "your-unique-name"

fly secrets set REDIS_URL="redis://default:PASSWORD@your-redis.fly.io:6379"
fly deploy
```

**Step 5: Test it**
```bash
# Your server is now live at https://your-app-name.fly.dev
curl https://your-app-name.fly.dev/health

# WebSocket:
wscat -c "wss://your-app-name.fly.dev/ws?user_id=alice"
```

**Free tier limits:**
- 3 shared-cpu-1x VMs, 256MB RAM each
- 3GB outbound data/month
- Redis: 256MB storage

---

### Option 2: Railway

Railway has a free starter plan that works well for this.

**Step 1: Push code to GitHub**
```bash
git init && git add . && git commit -m "init"
gh repo create notification-server --public --push
```

**Step 2: Deploy on Railway**
1. Go to [railway.app](https://railway.app) and sign in with GitHub
2. Click **New Project** → **Deploy from GitHub repo**
3. Select your repo
4. Click **Add Plugin** → choose **Redis**
5. Set environment variable:
   - `REDIS_URL` = `${{Redis.REDIS_URL}}` (Railway injects this automatically)

Railway auto-detects the `Dockerfile` and builds it.

**Free tier limits:**
- $5/month in credits (enough for ~500 hours)
- 512MB RAM, 1 vCPU

---

### Option 3: Render

1. Push code to GitHub
2. Go to [render.com](https://render.com) → **New Web Service**
3. Connect your GitHub repo
4. Set **Runtime** to `Docker`
5. Add a **Redis** service (Render offers a free Redis instance)
6. Set env var `REDIS_URL` pointing to your Render Redis URL

**Free tier limits:**
- Services spin down after 15 minutes of inactivity (bad for WebSockets)
- Use Render only if you're okay with reconnection on first request

---

### Option 4: Oracle Cloud Free Tier (Always Free — Best for 24/7)

Oracle gives you **2 free VMs forever** with 1GB RAM each. Good for always-on.

1. Sign up at [oracle.com/cloud/free](https://oracle.com/cloud/free)
2. Create a VM (Ubuntu 22.04, shape `VM.Standard.E2.1.Micro`)
3. SSH in, install Docker:
   ```bash
   sudo apt update && sudo apt install docker.io docker-compose -y
   sudo usermod -aG docker $USER
   ```
4. Clone your repo and run:
   ```bash
   git clone <your-repo>
   cd notification-server
   docker-compose up -d
   ```
5. Open port 8080 in the Oracle security list
6. Done — runs 24/7 forever for free

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
