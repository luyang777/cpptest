# Listener

A Windows application that receives JSON log messages from local applications using UDP, processes them with a C++ backend, and displays them in a real-time React dashboard.

## Build and run

### Prerequisites
- Windows 10/11
- Visual Studio 18 Community C++ build tools with a Windows SDK
- Node.js(LTS) and npm

### Backend 

Open a terminal :

```powershell
cd C:\Projects\cpptest\backend
.\build.cmd
.\listener.exe
```
The backend listens for UDP messages on 127.0.0.1:5000. Should see below message in terminal:

Log listener started on 127.0.0.1:5000
Dashboard API and WebSocket available on 127.0.0.1:5001

### Frontend

Open a second terminal:

```powershell
cd C:\Projects\cpptest\frontend
npm.cmd install
npm.cmd run dev
```

Open the URL `http://localhost:5173`.

## Test messages

The sender application is optional. Messages can also be sent directly from PowerShell.

### Valid message

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$msg = '{"timestamp":1755544291,"app":"TEST_APP","message":"Hello from terminal"}'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
$client.Send($bytes, $bytes.Length, "127.0.0.1", 5000)
$client.Close()
```

### Missing field

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$msg = '{"timestamp":1755544292,"app":"TEST_APP"}'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
$client.Send($bytes, $bytes.Length, "127.0.0.1", 5000)
$client.Close()
```

### Invalid JSON

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$msg = 'This is not JSON'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
$client.Send($bytes, $bytes.Length, "127.0.0.1", 5000)
$client.Close()
```

### Out-of-order message

```powershell
$client = New-Object System.Net.Sockets.UdpClient
$msg = '{"timestamp":1755544200,"app":"TEST_APP","message":"Older message"}'
$bytes = [System.Text.Encoding]::UTF8.GetBytes($msg)
$client.Send($bytes, $bytes.Length, "127.0.0.1", 5000)
$client.Close()
```

Malformed, incomplete, and out-of-order messages remain visible in the dashboard with an appropriate status.

## Optional test sender

To build and run the C++ test sender:

```powershell
cd C:\Projects\cpptest\sender
.\build.cmd
.\sender.exe
```

## Features

- UDP log listener on `127.0.0.1:5000`
- Real-time updates through WebSocket
- JSON validation
- Malformed, incomplete, and out-of-order messages remain visible
- Log file persistence
- Sorting and filtering
- Clear log

## API

- `GET http://127.0.0.1:5001/api/logs` returns all received records, including their status, as a JSON array.
- `DELETE http://127.0.0.1:5001/api/logs` clears the in-memory list and truncates the log file.
- `ws://127.0.0.1:5001/ws` sends `snapshot`, `log`, and `clear` events to connected dashboards.

See [DESIGN.md](DESIGN.md) for the architecture diagram and packet-handling design.

## Log files

- `logs/network.log` contains structurally valid packets, including packets marked `Out of order`.
- `logs/error.log` contains packets with missing fields, incorrect field types, or invalid JSON.
