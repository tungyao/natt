-- NATMesh Server Database Schema
-- SQLite

CREATE TABLE IF NOT EXISTS users (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    username    TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS devices (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id         TEXT NOT NULL UNIQUE,
    user_id         INTEGER NOT NULL,
    device_name     TEXT NOT NULL,
    public_key      TEXT NOT NULL DEFAULT '',
    public_ip       TEXT NOT NULL DEFAULT '',
    public_port     INTEGER NOT NULL DEFAULT 0,
    lan_ips         TEXT NOT NULL DEFAULT '[]',  -- JSON array
    virtual_ip      TEXT NOT NULL DEFAULT '',
    online          INTEGER NOT NULL DEFAULT 0,
    last_heartbeat  TEXT,
    created_at      TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS networks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    owner_id    INTEGER NOT NULL,
    subnet      TEXT NOT NULL DEFAULT '10.0.0.0/24',
    created_at  TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS network_devices (
    network_id  INTEGER NOT NULL,
    device_id   INTEGER NOT NULL,
    joined_at   TEXT NOT NULL DEFAULT (datetime('now')),
    PRIMARY KEY (network_id, device_id),
    FOREIGN KEY (network_id) REFERENCES networks(id) ON DELETE CASCADE,
    FOREIGN KEY (device_id) REFERENCES devices(id) ON DELETE CASCADE
);

CREATE INDEX idx_devices_user_id ON devices(user_id);
CREATE INDEX idx_devices_node_id ON devices(node_id);
CREATE INDEX idx_network_devices_network ON network_devices(network_id);
CREATE INDEX idx_network_devices_device ON network_devices(device_id);
