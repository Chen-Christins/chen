#!/usr/bin/env bash
#
# drain / 热重载集成测试（HTTP/1.1 与 HTTP/2）
#
# 通过环境变量 CHEN_IT_PROTO 选择协议：http（默认）或 http2（h2c prior knowledge）。
# curl 不支持 HTTP/2 时脚本以 77 退出，CTest 记为 SKIP。
#
# 场景：
#   1. 启动一个带 drain_test_module 的 HTTP server（master/worker 模型）
#   2. 发起一个耗时 800ms 的请求
#   3. 请求在途时给 master 发 SIGHUP（触发 fork 新 worker + 旧 worker drain）
#   4. 断言：在途请求仍拿到完整响应；新请求仍能被处理；旧 worker 在 drain 超时内退出
#
# 依赖：curl、python3（选空闲端口）。只能在 Linux 上运行。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SERVER_BIN="${ROOT_DIR}/bin/server"
MODULE_DIR="${SCRIPT_DIR}/module"
MODULE_SO="${MODULE_DIR}/libdrain_test_module.so"

PROTO="${CHEN_IT_PROTO:-http}"
CURL_OPTS=()

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/chen-drain-XXXXXX")"
START_AT=$(date +%s%3N)
MASTER_PID=""
CURL_PID=""
PORT=""

log() { printf '[drain-test] %s\n' "$*"; }

dump_logs() {
    if [[ -f "${TMP_DIR}/server.log" ]]; then
        printf '%s\n' '----- server.log (tail 80) -----' >&2
        tail -n 80 "${TMP_DIR}/server.log" >&2 || true
        printf '%s\n' '--------------------------------' >&2
    fi
}

fail() {
    printf '[drain-test] FAIL: %s\n' "$*" >&2
    dump_logs
    exit 1
}

cleanup() {
    if [[ -n "${CURL_PID}" ]] && kill -0 "${CURL_PID}" 2>/dev/null; then
        kill "${CURL_PID}" 2>/dev/null || true
        wait "${CURL_PID}" 2>/dev/null || true
    fi
    if [[ -n "${MASTER_PID}" ]] && kill -0 "${MASTER_PID}" 2>/dev/null; then
        kill -TERM "${MASTER_PID}" 2>/dev/null || true
        for _ in $(seq 1 50); do
            kill -0 "${MASTER_PID}" 2>/dev/null || break
            sleep 0.1
        done
        kill -KILL "${MASTER_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

http_code() {
    curl "${CURL_OPTS[@]}" -s -o /dev/null -w '%{http_code}' --max-time "${2:-2}" "http://127.0.0.1:${PORT}$1" 2>/dev/null || true
}

# ---------------------------------------------------------------- 前置检查
command -v curl >/dev/null 2>&1 || fail "curl not found"
command -v python3 >/dev/null 2>&1 || fail "python3 not found"
[[ -x "${SERVER_BIN}" ]] || fail "server not built: ${SERVER_BIN}（先构建 server 目标）"
[[ -f "${MODULE_SO}" ]] || fail "module not built: ${MODULE_SO}（先构建 drain_test_module 目标）"

if [[ "${PROTO}" == "http2" ]]; then
    if ! curl --version 2>/dev/null | head -n 1 | grep -qi 'HTTP2'; then
        if [[ "${CHEN_IT_REQUIRE_HTTP2:-0}" == "1" ]]; then
            fail "当前 curl 不支持 HTTP/2，但 CHEN_IT_REQUIRE_HTTP2=1（CI 要求必须验证）"
        fi
        echo "[drain-test] SKIP: 当前 curl 不支持 HTTP/2（需要 nghttp2 支持）"
        exit 77
    fi
    CURL_OPTS+=(--http2-prior-knowledge)
fi

# ---------------------------------------------------------------- 选端口 + 写配置
PORT="$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)" || fail "failed to pick a free port"
[[ -n "${PORT}" ]] || fail "empty port"

# 默认 root logger 级别是 WARN（见 LoggerManager 构造函数），不配 log.yml 时
# INFO 级日志会被过滤，脚本就没法断言 drain / shutdown 的关键路径。
cat > "${TMP_DIR}/log.yml" <<'EOF'
logs:
  - name: root
    level: info
    formatter: "%d{%Y-%m-%d %H:%M:%S.%f}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    appenders:
      - type: StdoutLogAppender
EOF

cat > "${TMP_DIR}/system.yml" <<EOF
server:
  work_path: "${TMP_DIR}"
  pid_file: "chen.pid"
  drain_timeout_ms: 5000
  housekeeping_interval_ms: 100
workers:
  io:
    thread_num: 1
  accept:
    thread_num: 1
module:
  path: "${MODULE_DIR}"
servers:
  - address: ["127.0.0.1:${PORT}"]
    keepalive: 1
    timeout: 30000
    name: drain_http
    accept_worker: accept
    io_worker: io
    process_worker: io
    type: ${PROTO}
EOF

# ---------------------------------------------------------------- 启动
log "starting ${PROTO} server on 127.0.0.1:${PORT} (work_path=${TMP_DIR})"
"${SERVER_BIN}" -s -c "${TMP_DIR}" > "${TMP_DIR}/server.log" 2>&1 &
MASTER_PID=$!

READY=0
LAST_CODE=""
for _ in $(seq 1 100); do
    LAST_CODE="$(http_code /drain/fast 1)"
    if [[ "${LAST_CODE}" == "200" ]]; then
        READY=1
        break
    fi
    kill -0 "${MASTER_PID}" 2>/dev/null || fail "master exited during startup"
    sleep 0.1
done
[[ "${READY}" == "1" ]] || fail "server did not become ready within 10s (last status=${LAST_CODE:-none})"
log "server ready (master pid=${MASTER_PID})"

# servlet 没注册时探活也会失败，但只有 body 断言能把“404 默认页”和“路由正确”区分开
FAST_RC=0
FAST_BODY="$(curl "${CURL_OPTS[@]}" -s --max-time 2 "http://127.0.0.1:${PORT}/drain/fast")" || FAST_RC=$?
[[ ${FAST_RC} -eq 0 ]] || fail "/drain/fast request failed (curl exit=${FAST_RC})"
[[ "${FAST_BODY}" == "fast-ok" ]] || fail "unexpected /drain/fast body: '${FAST_BODY}'"

# ---------------------------------------------------------------- 流式响应
# H1：sendResponseHeader 写头后 sendData 直接写 socket，处理完关闭连接；
# H2：HEADERS 帧不带 END_STREAM，数据走 DATA 帧，最后一段带 END_STREAM
# set -e 下 curl 失败会直接退出、跳过 fail() 的日志转储，所以这里显式收 exit code
STREAM_RC=0
STREAM_BODY="$(curl "${CURL_OPTS[@]}" -s --max-time 10 "http://127.0.0.1:${PORT}/drain/stream")" || STREAM_RC=$?
[[ ${STREAM_RC} -eq 0 ]] || fail "streaming request failed (curl exit=${STREAM_RC}, body='${STREAM_BODY}')"
for chunk in chunk-1 chunk-2 chunk-3; do
    [[ "${STREAM_BODY}" == *"${chunk}"* ]] || fail "streaming body missing '${chunk}' (body='${STREAM_BODY}')"
done
log "streaming response ok (${#STREAM_BODY} bytes): $(printf '%s' "${STREAM_BODY}" | tr '\n' ' ')"

# SSE：SSESession 走 sendResponseHeader()/sendData()，H1/H2 同一份业务代码
SSE_RC=0
SSE_BODY="$(curl "${CURL_OPTS[@]}" -s --max-time 10 "http://127.0.0.1:${PORT}/drain/sse")" || SSE_RC=$?
[[ ${SSE_RC} -eq 0 ]] || fail "sse request failed (curl exit=${SSE_RC}, body='${SSE_BODY}')"
for expect in "data: sse-1" "event: msg" ": ping" "data: sse-2"; do
    [[ "${SSE_BODY}" == *"${expect}"* ]] || fail "sse body missing '${expect}' (body='${SSE_BODY}')"
done
log "sse response ok: $(printf '%s' "${SSE_BODY}" | tr '\n' ' ')"

# ---------------------------------------------------------------- HTTP 指标
# HttpServer 在 dispatch 前后自动打点，/drain/metrics 由 MetricsServlet 渲染 Prometheus 文本
METRICS_RC=0
METRICS_BODY="$(curl "${CURL_OPTS[@]}" -s --max-time 5 "http://127.0.0.1:${PORT}/drain/metrics")" || METRICS_RC=$?
[[ ${METRICS_RC} -eq 0 ]] || fail "metrics request failed (curl exit=${METRICS_RC})"
[[ "${METRICS_BODY}" == *"http_requests_total"* ]] || fail "metrics missing http_requests_total"
[[ "${METRICS_BODY}" == *'path="/drain/fast"'* ]] || fail "metrics missing /drain/fast series"
[[ "${METRICS_BODY}" == *'status="200"'* ]] || fail "metrics missing status=200 series"
[[ "${METRICS_BODY}" == *"http_requests_in_flight"* ]] || fail "metrics missing http_requests_in_flight"
log "http metrics ok"

# ---------------------------------------------------------------- 在途慢请求 + SIGHUP
log "sending slow request (800ms) and SIGHUP mid-flight"
curl "${CURL_OPTS[@]}" -s --max-time 20 -o "${TMP_DIR}/slow.body" -w '%{http_code}' \
    "http://127.0.0.1:${PORT}/drain/slow?ms=800" > "${TMP_DIR}/slow.code" &
CURL_PID=$!

sleep 0.25
kill -HUP "${MASTER_PID}"

set +e
wait "${CURL_PID}"
CURL_RC=$?
set -e
CURL_PID=""

SLOW_CODE="$(cat "${TMP_DIR}/slow.code" 2>/dev/null || true)"
SLOW_BODY="$(cat "${TMP_DIR}/slow.body" 2>/dev/null || true)"

[[ "${CURL_RC}" -eq 0 ]] || fail "slow request aborted during reload (curl rc=${CURL_RC}, code=${SLOW_CODE})"
[[ "${SLOW_CODE}" == "200" ]] || fail "slow request status=${SLOW_CODE} (expected 200)"
[[ "${SLOW_BODY}" == "slow-ok" ]] || fail "slow request body='${SLOW_BODY}' (expected slow-ok)"
log "in-flight request completed: status=${SLOW_CODE} body=${SLOW_BODY}"

# ---------------------------------------------------------------- reload 后仍可服务
NEW_CODE=""
for _ in $(seq 1 50); do
    NEW_CODE="$(http_code /drain/fast 1)"
    [[ "${NEW_CODE}" == "200" ]] && break
    sleep 0.1
done
[[ "${NEW_CODE}" == "200" ]] || fail "server not serving after reload (last status=${NEW_CODE})"
log "server still serving after reload"

# ---------------------------------------------------------------- 不应触发 drain 超时
if grep -q "drain timeout" "${TMP_DIR}/server.log"; then
    fail "drain timed out (see log): in-flight requests were force-closed"
fi

# ---------------------------------------------------------------- 优雅停止
log "sending SIGTERM and waiting for master exit"
STOP_AT=$(date +%s%3N)
kill -TERM "${MASTER_PID}"
STOPPED=0
for _ in $(seq 1 100); do
    if ! kill -0 "${MASTER_PID}" 2>/dev/null; then
        STOPPED=1
        break
    fi
    sleep 0.1
done
[[ "${STOPPED}" == "1" ]] || fail "master did not exit within 10s after SIGTERM"
MASTER_PID=""
STOP_MS=$(( $(date +%s%3N) - STOP_AT ))

grep -q "graceful shutdown complete" "${TMP_DIR}/server.log" || fail "graceful shutdown did not complete cleanly"
if [[ "${PROTO}" == "http2" ]]; then
    grep -q "H2 session drained" "${TMP_DIR}/server.log" || fail "H2 session not drained via GOAWAY (see log)"
fi
# 结构性断言：master 退出时应清掉 pid 文件（不依赖日志级别）
[[ ! -f "${TMP_DIR}/chen.pid" ]] || fail "pid file was not cleaned up on exit"

TOTAL_MS=$(( $(date +%s%3N) - START_AT ))
log "PASS [${PROTO}] (total ${TOTAL_MS} ms, shutdown ${STOP_MS} ms)"
