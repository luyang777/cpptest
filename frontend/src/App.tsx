import { useEffect, useRef, useState } from "react";
import { AgGridReact } from "ag-grid-react";
import {
  AllCommunityModule,
  ModuleRegistry,
  type ColDef,
  type ICellRendererParams,
} from "ag-grid-community";
import "./App.css";
import "ag-grid-community/styles/ag-grid.css";
import "ag-grid-community/styles/ag-theme-quartz.css";

ModuleRegistry.registerModules([AllCommunityModule]);

type LogRecord = {
  id: number;
  timestamp: number | null;
  app: string | null;
  message: string | null;
  status: string;
};
const API_URL = "http://127.0.0.1:5001/api/logs";
const WEBSOCKET_URL = "ws://127.0.0.1:5001/ws";

function formatTimestamp(timestamp: number | null) {
  if (timestamp == null) return "Missing";
  const date = new Date(timestamp * 1000);
  const pad = (value: number) => value.toString().padStart(2, "0");
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function normalizeRecord(record: Partial<LogRecord>, fallbackId: number): LogRecord {
  const hasValidFields = record.timestamp != null && Boolean(record.app) && Boolean(record.message);
  return {
    id: record.id ?? fallbackId,
    timestamp: record.timestamp ?? null,
    app: record.app ?? null,
    message: record.message ?? null,
    status: record.status ?? (hasValidFields ? "Valid" : "Invalid packet"),
  };
}

function App() {
  const [records, setRecords] = useState<LogRecord[]>([]);
  const [connected, setConnected] = useState(false);
  const gridRef = useRef<AgGridReact<LogRecord>>(null);

  useEffect(() => {
    let socket: WebSocket | null = null;
    let reconnectTimer: number | undefined;
    let disposed = false;

    const connect = () => {
      if (disposed) return;
      // One persistent socket carries every event; reconnect only after it closes.
      socket = new WebSocket(WEBSOCKET_URL);
      socket.addEventListener("open", () => setConnected(true));
      socket.addEventListener("close", () => {
        setConnected(false);
        socket = null;
        if (!disposed) reconnectTimer = window.setTimeout(connect, 1000);
      });
      socket.addEventListener("error", () => setConnected(false));
      socket.addEventListener("message", (event) => {
        const update = JSON.parse(event.data) as {
          type: "snapshot" | "log" | "clear";
          records?: LogRecord[];
          record?: LogRecord;
        };

        if (update.type === "snapshot" && update.records) {
          setRecords(update.records.map((record, index) => normalizeRecord(record, index + 1)));
        } else if (update.type === "log" && update.record) {
          const record = normalizeRecord(update.record, Date.now());
          setRecords((currentRecords) => [...currentRecords, record]);
        } else if (update.type === "clear") {
          setRecords([]);
        }
      });
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer !== undefined) window.clearTimeout(reconnectTimer);
      socket?.close();
    };
  }, []);

  const clearLogs = async () => {
    await fetch(API_URL, { method: "DELETE" });
    setRecords([]);
  };

  const clearFilters = () => {
    gridRef.current?.api.setFilterModel(null);
  };

  const columnDefs: ColDef<LogRecord>[] = [
    {
      field: "timestamp",
      headerName: "Timestamp",
      width: 225,
      filter: "agDateColumnFilter",
      filterParams: {
          comparator: (filterDate: Date, timestamp: number | null) => {
          if (timestamp == null) return -1;

          const cellDate = new Date(timestamp * 1000);
          const cellDay = new Date(
            cellDate.getFullYear(),
            cellDate.getMonth(),
            cellDate.getDate(),
          );

          if (cellDay < filterDate) return -1;
          if (cellDay > filterDate) return 1;
          return 0;
        },
      },
      valueFormatter: ({ value }) => formatTimestamp(value),
    },
    {
      field: "app",
      headerName: "Application",
      width: 210,
      filter: "agTextColumnFilter",
      cellRenderer: ({ value }: ICellRendererParams<LogRecord, string | null>) => (
        <span className="app-tag">{value ?? "Missing"}</span>
      ),
    },
    {
      field: "message",
      headerName: "Message",
      flex: 1,
      filter: "agTextColumnFilter",
      cellRenderer: ({ value }: ICellRendererParams<LogRecord, string | null>) => (
        <span className="message">{value ?? "Missing"}</span>
      ),
    },
    {
      field: "status",
      headerName: "Status",
      width: 170,
      filter: "agTextColumnFilter",
      cellRenderer: ({ value }: ICellRendererParams<LogRecord, string | null>) => (
        <span className={`packet-status ${(value ?? "").toLowerCase().startsWith("valid") ? "valid" : (value ?? "").toLowerCase() === "out of order" ? "warning" : "invalid"}`}>
          {value ?? "Unknown"}
        </span>
      ),
    },
  ];

  const defaultColDef: ColDef<LogRecord> = {
    sortable: true,
    resizable: true,
    floatingFilter: true,
  };

  const emptyState = (
    <div className="empty">
      <span>o</span>
      <strong>Listening for events</strong>
      <small>
        Packets sent to 127.0.0.1:5000 will appear here.
      </small>
    </div>
  );

  return (
    <main className="dashboard">
      <header className="topbar">
        <div className="brand">
          <span className="brand-mark">~</span>
          <span>Listener</span>
        </div>
        <div className={`status ${connected ? "online" : ""}`}>
          <span className="status-dot" />
          {connected ? "Listener online" : "Waiting for listener"}
        </div>
      </header>
      <section className="intro">
        <div>
          <p className="eyebrow">LOCALHOST / UDP 5000</p>
          <h1>
            Network log
            <br />
            <em>monitor.</em>
          </h1>
        </div>
        <div className="stats">
          <div>
            <strong>{records.length}</strong>
            <span>events captured</span>
          </div>
          <div>
            <strong>{new Set(records.map((record) => record.app).filter(Boolean)).size}</strong>
            <span>active sources</span>
          </div>
        </div>
      </section>
      <section className="log-panel">
        <div className="toolbar">
          <div className="toolbar-title">
            <span className="pulse" />
            Live event stream
          </div>
          <div className="actions">
            <button
              type="button"
              className="sort-button"
              onClick={clearFilters}
            >
              Clear filters
            </button>
            <button
              type="button"
              className="clear-button"
              onClick={() => void clearLogs()}
            >
              Clear log
            </button>
          </div>
        </div>
        <div className="table-wrap">
          {records.length === 0 ? (
            emptyState
          ) : (
            <div className="ag-theme-quartz log-grid">
              <AgGridReact
                ref={gridRef}
                theme="legacy"
                columnDefs={columnDefs}
                defaultColDef={defaultColDef}
                rowData={records}
                getRowId={({ data }) => String(data.id)}
              />
            </div>
          )}
        </div>
        <footer className="panel-footer">
          <span>
            Showing {records.length} events
          </span>
          <span className="endpoint">● 127.0.0.1:5000</span>
        </footer>
      </section>
    </main>
  );
}

export default App;
