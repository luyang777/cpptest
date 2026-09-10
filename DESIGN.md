# Listener Design

Local apps
       |
       | JSON over UDP :5000
       v
C++ Listener
       |
       +--> Validate and classify
       |       |
       |       +--> Valid or out of order -> logs/network.log
       |       +--> Missing, invalid, or malformed -> logs/error.log
       |
       +--> In-memory records
                     |
                     +--> WebSocket :5001/ws -> React + AG Grid
                     |                         live updates, filtering, sorting
                     |
                     +--> HTTP API :5001       GET and clear records

## Packet statuses

- `Valid`: all required fields have the correct types.
- `Out of order`: valid packet with an older timestamp; still written to `network.log`.
- `Missing ...` or `Invalid ...`: required field is missing or has the wrong type; written to `error.log`.
- `Invalid JSON`: payload cannot be parsed; written to `error.log`.

Every packet is sent to the dashboard, including invalid packets. Missing values are displayed as `Missing`.
