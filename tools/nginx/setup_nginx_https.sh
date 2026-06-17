#!/bin/bash
# tools/nginx/setup_nginx_https.sh
#
# Idempotent setup of nginx as reverse proxy in front of:
#   - Vite on 127.0.0.1:5173  (React dashboard, /)
#   - Flask on 127.0.0.1:8000 (REST + artifacts, /api /artifact /socket.io /output)
#
# Outputs:
#   - Self-signed 10y SSL cert + key in   $HOME/.chrono3d/nginx/
#   - Nginx config copied to              $HOME/.chrono3d/nginx/nginx.conf
#   - Nginx process running, listening 443 on 0.0.0.0
#
# Cleanup:
#   pkill -f "nginx -c .*chrono3d";   rm -rf $HOME/.chrono3d/nginx/

set -euo pipefail

# ── Distinct exit codes ──────────────────────────────────────────────────
EXIT_OK=0
EXIT_NO_VITE=2        # upstream services not running
EXIT_PORT_BUSY=3      # port 443/80 already bound by another nginx
EXIT_NO_TLS_CERT=4    # cert generation or perm failed
EXIT_RELOAD_FAILED=5  # nginx -t passed but launch failed
EXIT_NEEDS_SUDO=6     # requires CAP_NET_BIND_SERVICE

# ── Paths (env-overridable) ───────────────────────────────────────────────
NGINX_DIR="${CHRONON_NGINX_DIR:-$HOME/.chrono3d/nginx}"
CERT="$NGINX_DIR/cert.pem"
KEY="$NGINX_DIR/key.pem"
HTPW="$NGINX_DIR/.htpasswd"
AUTH_FILE="$NGINX_DIR/auth.txt"
DST_CONF="$NGINX_DIR/nginx.conf"
PIDFILE="$NGINX_DIR/nginx.pid"
SRC_CONF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/nginx.conf"

echo "[1] ensure nginx + openssl + apache2-utils installed"
# Try direct apt-get; on Permission denied, fall back to sudo -n (passwordless)
pkg_install() {
    if apt-get "$@" 2>/tmp/apt.err; then return 0; fi
    if grep -qiE "lock|permission denied" /tmp/apt.err 2>/dev/null && command -v sudo >/dev/null; then
        echo "    apt-get denied — retrying with sudo -n"
        sudo -n apt-get "$@" 2>/tmp/apt.err && return 0
    fi
    return 1
}
if ! command -v nginx >/dev/null; then
    pkg_install update -qq || true
    pkg_install install -y --no-install-recommends nginx openssl \
        || { echo "FATAL: cannot install nginx (see /tmp/apt.err)"; rm -f /tmp/apt.err; exit "$EXIT_NEEDS_SUDO"; }
fi
rm -f /tmp/apt.err
command -v nginx >/dev/null || { echo "FATAL: nginx still missing after install"; exit "$EXIT_NEEDS_SUDO"; }
command -v openssl >/dev/null || { echo "FATAL: openssl not installed"; exit "$EXIT_NEEDS_SUDO"; }

echo "[2] ensure runtime dir + log files"
mkdir -p "$NGINX_DIR"/{client_body,proxy,fastcgi,uwsgi,scgi}
touch "$NGINX_DIR/error.log" "$NGINX_DIR/access.log"
chmod 700 "$NGINX_DIR"

echo "[3] generate self-signed cert (3650 days, idempotent)"
if [ ! -f "$CERT" ] || [ ! -f "$KEY" ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
        -keyout "$KEY" -out "$CERT" \
        -subj "/CN=${PUBLIC_IP:-chrono3d.local}/O=Chronon3D Telemetry" \
    && chmod 600 "$KEY" \
    || { echo "FATAL: cert generation failed"; exit "$EXIT_NO_TLS_CERT"; }
else
    echo "    cert exists, reusing"
fi

echo "[4] (no basic-auth — dashboard is open)"
# Cleanup legacy htpasswd artifacts from previous setups
rm -f "$HTPW" "$AUTH_FILE" 2>/dev/null || true

echo "[5] install + syntax-check nginx.conf (port ${CHRONON_NGINX_PORT:-443}, redirect 80 disables on high port)"
cp -f "$SRC_CONF" "$DST_CONF"

# If user picked a high port (≠443), substitute :443 in config and disable the HTTP→HTTPS redirect
LISTEN_PORT="${CHRONON_NGINX_PORT:-443}"
if [ "$LISTEN_PORT" != "443" ]; then
    echo "    substituting listen port: 443 → $LISTEN_PORT (and disabling :80 redirect)"
    sed -i -E \
        -e "s|listen 0.0.0.0:443 ssl|listen 0.0.0.0:${LISTEN_PORT} ssl|" \
        -e "/listen 80 default_server/,/return 301 https/d" \
        -e "s/return 301 https:.*/return 301 https:\\/\\/${LISTEN_PORT}/g" \
        "$DST_CONF"
fi

set +e
NGINX_T_OUT=$(nginx -t -c "$DST_CONF" -p "$NGINX_DIR/" 2>&1)
NGINX_T_RC=$?
set -e
printf '%s\n' "$NGINX_T_OUT"
if [ "$NGINX_T_RC" -ne 0 ]; then
    # Three distinct failures, each with its own exit code:
    if printf '%s' "$NGINX_T_OUT" | grep -qE "bind\(\) to .*:443 failed \(13: Permission denied\)"; then
        echo ""
        echo "      ── nginx cannot bind to port 443.  Choose ONE: ──"
        echo "      │ (a) one-shot (no root):  sudo setcap cap_net_bind_service=+ep \$(which nginx)"
        echo "      │ (b) run this script as root"
        echo "      │ (c) use a high port:    CHRONON_NGINX_PORT=8443 bash $0"
        echo "      └──────────────────────────────────────────────────"
        exit "$EXIT_NEEDS_SUDO"
    fi
    if printf '%s' "$NGINX_T_OUT" | grep -qE "bind\(\) to .*:80? failed \(98: Address already in use\)" \
       || printf '%s' "$NGINX_T_OUT" | grep -qE "bind\(\) to .*:${LISTEN_PORT} failed \(98: Address already in use\)"; then
        echo ""
        echo "      ── port already in use.  Find the squatter: ──"
        echo "      │    ss -tlnp4 | grep ':${LISTEN_PORT} \\|:80 '"
        echo "      │ Kill it (or stop system nginx: service nginx stop, systemctl stop nginx)"
        echo "      │ Or use a different port:  CHRONON_NGINX_PORT=8443 bash $0"
        echo "      └──────────────────────────────────────────────────"
        exit "$EXIT_PORT_BUSY"
    fi
    echo "FATAL: nginx -t failed on $DST_CONF"
    exit "$EXIT_RELOAD_FAILED"
fi
echo "    nginx -t: syntax OK"

echo "[6] detect CAP_NET_BIND_SERVICE and start nginx"
NEEDS_CAP=false
if [ "$(id -u)" -ne 0 ]; then
    if ! getcap "$(command -v nginx)" 2>/dev/null | grep -q cap_net_bind_service; then
        NEEDS_CAP=true
    fi
fi

# Pre-kill any prior instance from this script
if [ -f "$PIDFILE" ]; then
    OLD_PID=$(cat "$PIDFILE" 2>/dev/null || true)
    if [ -n "${OLD_PID:-}" ] && kill -0 "$OLD_PID" 2>/dev/null; then
        echo "    killing prior nginx PID=$OLD_PID"
        kill -KILL "$OLD_PID" 2>/dev/null || true
        sleep 1
    fi
fi
pkill -9 -f "nginx -c $DST_CONF" 2>/dev/null || true
sleep 1

# Wait for port 443 to actually be free
echo "    waiting for port 443 to be free…"
for _ in $(seq 1 20); do
    if ! ss -tlnp4 | grep -q ':443 '; then break; fi
    sleep 0.5
done

set +e
nginx -c "$DST_CONF" -p "$NGINX_DIR/" 2>/tmp/nginx_launch.err
LAUNCH_RC=$?
set -e

if [ "$LAUNCH_RC" -ne 0 ]; then
    echo "FATAL: nginx launch failed (rc=$LAUNCH_RC)"
    if grep -qiE "permission denied|cap_net_bind" /tmp/nginx_launch.err 2>/dev/null; then
        echo ""
        echo "      ─────────────────────────────────────────────────"
        echo "      │ nginx cannot bind 443 without root or CAP_NET_BIND_SERVICE."
        echo "      │ One-shot fix (no root needed):"
        echo "      │   sudo setcap cap_net_bind_service=+ep \$(command -v nginx)"
        echo "      │ Or run this script with:  CHRONON_NGINX_PORT=8443 bash setup_nginx_https.sh"
        echo "      └─────────────────────────────────────────────────"
        exit "$EXIT_NEEDS_SUDO"
    fi
    cat /tmp/nginx_launch.err
    exit "$EXIT_RELOAD_FAILED"
fi
rm -f /tmp/nginx_launch.err || true

# Wait until Vite/Flask upstreams are reachable; otherwise HTTPS / proxy will be 502
echo "[7] check upstream services (Vite:5173, Flask:8000)"
for svc in "5173:Vite" "8000:Flask"; do
    PORT="${svc%%:*}"; NAME="${svc##*:}"
    if curl -fsS -o /dev/null --max-time 2 "http://127.0.0.1:$PORT/" 2>/dev/null; then
        echo "    $NAME on 127.0.0.1:$PORT: UP"
    else
        echo "    WARN: $NAME on 127.0.0.1:$PORT not reachable.  HTTPS will 502 until it's up."
    fi
done

echo "[8] listen sockets (ss -tlnp4)"
ss -tlnp4 | grep -E ':(80|443) ' | head -5

echo "[9] verify HTTPS — no auth (expect 200 for dashboard and /api/runs)"
curl -sk -o /dev/null -w '    /                                 -> HTTP %{http_code}\n' --max-time 5 https://127.0.0.1:443/
curl -sk -o /dev/null -w '    /api/runs                         -> HTTP %{http_code}\n' --max-time 5 https://127.0.0.1:443/api/runs
curl -sk -o /dev/null -w '    /artifact?path=output/...mp4      -> HTTP %{http_code}\n' --max-time 5 "https://127.0.0.1:443/artifact?path=output/tilt_sweep_v2_clean.mp4"

echo ""
echo "DONE — nginx ready (no auth)."
echo ""
echo "Public URLs:"
echo "  https://${PUBLIC_IP:-51.222.204.158}/                       (Vite dashboard, public)"
echo "  https://${PUBLIC_IP:-51.222.204.158}/api/runs               (Flask API)"
echo "  https://${PUBLIC_IP:-51.222.204.158}/artifact?path=…         (Flask artifact)"
echo ""
echo "Note: cert is self-signed — browser will prompt NET::ERR_CERT_AUTHORITY_INVALID."
echo "      Accept the exception once.  For mkcert or Let's Encrypt see docs/nginx-tls.md (TODO)."
