# TLS 测试证书

本目录只用于 `tests/test_tls.cc`， **不要用于生产环境**。

| 文件                                | 用途                                               |
|-------------------------------------|----------------------------------------------------|
| `ca.crt` / `ca.key`                 | 测试 CA，客户端用它校验服务端                      |
| `server.crt` / `server.key`         | `CN=localhost`，SAN: `DNS:localhost, IP:127.0.0.1` |
| `wrong-host.crt` / `wrong-host.key` | SAN: `DNS:wrong.example.com`，用于主机名不匹配测试 |
| `unknown-ca.crt` / `unknown-ca.key` | 自签名证书，用于未知 CA 测试                       |
| `expired.crt` / `expired.key`       | CA 签发但已过期，用于过期证书测试                  |

## 重新生成

```bash
cd tests/certs
./gen.sh
```

要求：

- OpenSSL 1.1.1+
- 证书有效期 10 年；`expired.crt` 固定为 2019-2020，用于稳定的过期测试
- `expired.crt` 使用 `openssl ca -startdate/-enddate` 生成，兼容 OpenSSL 3.0（不使用 `x509 -not_before`）

## 说明

- 私钥是测试密钥，提交到仓库没有安全影响。
- 如果系统时间晚于 2036 年，需要重新生成 `ca.crt` / `server.crt` 等证书。
- `tests/CMakeLists.txt` 通过 `CHEN_TEST_CERT_DIR` 把本目录绝对路径传给测试程序。
