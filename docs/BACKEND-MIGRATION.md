# Backend migration assessment — RodlandFarmsAPI → local fleet

## ✅ CUTOVER COMPLETE — 2026-06-11

The backend is **live on the local fleet**. Stack in `~/Desktop/plantPulse/backend/`:
`rodland_api_app` (php:8.2) on `127.0.0.1:8003` + `rodland_api_db` (mysql:8.4) on
`3308`; system nginx vhost `athome.rodlandfarms.com` (HTTP proxied **without**
HTTPS redirect so the firmware's plain-HTTP POST works; LE cert via certbot
`--no-redirect`). Live code rsynced (prod hand-edits, not the stale GitHub repo);
prod DB imported; `APP_KEY` preserved. Outage fixed (Carbon bump) + MySQL 8.4
`sql_mode` fixed via `config/database.php` `modes` (not the vendor hack). Verified
end-to-end through the edge: app `GET /api/user/devices` (HTTPS) and firmware
`POST /api/esp/data` (HTTP) both return JSON; readings store.

**Remaining:** (1) refresh firmware OTA pinned cert (`include/cert.h`) to the new
LE chain — OTA HTTPS fails until then; (2) add athome to qstatus + a drift
tripwire; (3) decommission SiteGround (no data gap — it was 500-ing, not storing);
(4) optional container polish (skip `migrate`/add doctrine-dbal, `config:cache`,
set a compose project name).

---

**Original assessment (below) — superseded by the cutover above.**

Moving the PlantPulse backend
(`athome.rodlandfarms.com`, Laravel `RodlandFarmsAPI`) off cPanel shared hosting
onto the local Ubuntu fleet box (Docker behind the shared system nginx, alongside
the CRM, fitness, qr-generator, and tts stacks).

## Why move it

- **Root-cause fix for the current outage.** The shared host auto-upgraded PHP to
  8.2 and broke the pinned `nesbot/carbon`, 500-ing the whole API
  (`docs/DIAGNOSIS-2026-06-11.md`). On a self-managed container we pin the PHP
  version and dependencies — no surprise upgrades.
- **Joins the managed fleet.** Same nginx edge, Let's Encrypt certs, Docker
  deploy flow, drift tripwires, and **qstatus** monitoring as the other four apps.
- **Full control** of logs, DB, scaling, and secrets.

## Fleet context (target environment)

- Ubuntu 24.04; one system **nginx** is the only public face; every app is a Docker
  stack bound to `127.0.0.1`. Docker needs `sudo` on this host.
- **Ports already taken:** 8000 (CRM), 8001 (TTS), 8002 (fitness), 8090
  (qr-generator). Pick a new free one (e.g. **8003**).
- **Closest existing pattern to copy: `qr-generator`** (Laravel 9 + dedicated
  MySQL 8.4 in Docker) and `fitness` (Laravel + MySQL). Reuse their compose/nginx
  shape.
- Each app has its **own** datastore container — RodlandFarmsAPI gets its own MySQL.

## Migration plan (phased)

### Phase 0 — Recon / acquire (blocked on access — see open questions)
- Get the `RodlandFarmsAPI` **source** (git repo? or pull from the cPanel host).
- Read its `composer.json` for the **PHP version** + extensions it needs.
- Export the **MySQL data** (mysqldump / phpMyAdmin) — only the RodlandFarmsAPI
  tables (users, devices, readings, api_tokens). NOTE: the shared host's DB is a
  multi-app dump; do **not** drag in unrelated tables.
- Grab the **`.env`** (APP_KEY, DB, mail, any signing secrets).

### Phase 1 — Containerize
- Write a `Dockerfile` (PHP-FPM + composer) and `docker-compose.yml` (app + MySQL),
  modeled on `qr-generator/`. **Pin PHP** (e.g. 8.1 or 8.2) and **bump
  `nesbot/carbon` to ^2.72** — fixing the outage as part of the move.
- Bind the app to `127.0.0.1:8003` (container side per its own nginx/fpm config).
- Keep `APP_KEY` constant if any data is encrypted at rest.

### Phase 2 — Data migration
- Stand up the MySQL container, import the dump, run `php artisan migrate` if
  needed. **Preserve `api_token`s** — otherwise every deployed sensor must be
  re-provisioned.

### Phase 3 — Edge (nginx + TLS)
- Add an `athome.rodlandfarms.com` server block → `127.0.0.1:8003`, Let's Encrypt
  cert (same as the other 5 vhosts).
- Add to **qstatus**: `APP_NAMES`, `APP_PROBES`, `APP_EDGE_PROBES`, `CERT_DOMAINS`,
  `DRIFT_LOGS`. Add a drift tripwire if it's a prod checkout.

### Phase 4 — DNS cutover
- Lower the `athome.rodlandfarms.com` A-record TTL ahead of time, then repoint it
  to this box's public IP. Verify, then raise TTL back.

### Phase 5 — Verify end-to-end
- Device `POST /api/esp/data` stores a reading; app `/user/devices` +
  `/user/{host}/latest` return JSON; a real board provisions and shows data.

## Critical gotchas

- **Firmware needs NO change for the data endpoint** — same hostname, DNS just
  repoints. The data POST is plain HTTP, so unaffected by the new cert.
- **OTA cert pinning WILL break** — the firmware pins a cert in `include/cert.h`
  (`cert_pem2`) for the HTTPS OTA fetch. A new Let's Encrypt cert on the new host
  won't match the pinned root → OTA TLS fails (it's *already* failing, see
  diagnosis). Bundle this with the **P2 OTA fix**: refresh the pinned cert or
  switch the firmware to the ESP-IDF cert bundle (ISRG Root X1).
- **`api_token` preservation** is mandatory across the DB move (else mass
  re-provisioning).
- **Don't import the multi-app shared DB wholesale** — only RodlandFarmsAPI tables.
- **Resource headroom** — this box already runs four stacks + MySQLs; confirm RAM
  headroom before adding a fifth.

## Open questions (need answers to make this concrete)

1. Is `RodlandFarmsAPI` in a **git repo** (e.g. under `github.com/c1scok1d/`), or
   only living on the cPanel host?
2. Do you have **DB export access** (SSH / phpMyAdmin / mysqldump) on the current
   host?
3. Who controls **DNS** for `rodlandfarms.com` (registrar / Cloudflare), and can we
   repoint the `athome` A-record?
4. Is this box's **public IP** the same one already serving the other vhosts? (If
   so, the new vhost just slots into the existing nginx.)
5. Target **PHP version** the app is known-good on (from `composer.json` / current
   host) so we pin the container correctly.

## Recommendation

Worth doing — it permanently removes the shared-host fragility that caused this
outage and brings the backend under the same monitoring/deploy discipline as the
rest of the fleet. **But the fastest path to readings flowing again is the
one-line Carbon bump on the current host** (`docs/DIAGNOSIS-2026-06-11.md`); do
that first to restore service, then migrate deliberately rather than under outage
pressure.
